#include "features/bhaptics/body_haptics.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace evr::bhaptics {

namespace {

// Each weapon class's kick: the sleeve's intensity and length, and the chest's share of it.
struct Kick {
    float sleeve;
    int millis;
    float chest;
};

constexpr Kick kickOf(WeaponClass weapon) {
    switch (weapon) {
    case WeaponClass::Light:
        return {35.0f, 60, 0.4f};
    case WeaponClass::Medium:
        return {65.0f, 90, 0.5f};
    case WeaponClass::Heavy:
        return {100.0f, 130, 0.7f};
    case WeaponClass::Huge:
        return {100.0f, 350, 0.9f};
    }
    return {65.0f, 90, 0.5f};
}

// Damage: the intensity for an amount of health plus armor lost.
constexpr float kDamageFloor = 35.0f;
constexpr float kDamagePerPoint = 1.3f;
constexpr int kDamageMillis = 150;
constexpr float kUndirectedShare = 0.8f;
// The heartbeat's period at kLowHealth and near 0 health, the gap from lub to dub, and their strengths.
constexpr double kBeatSlowSeconds = 1.1;
constexpr double kBeatFastSeconds = 0.6;
constexpr double kDubSeconds = 0.18;
constexpr float kLub = 80.0f;
constexpr float kDub = 50.0f;
constexpr int kBeatMillis = 100;
constexpr float kGloryKill = 80.0f;
constexpr int kGloryKillMillis = 250;
constexpr float kDeath = 100.0f;
constexpr int kDeathMillis = 700;
// The Flame Belch (a burst while its button is held: at most one pulse every kBelchGapSeconds) and the
// equipment launcher.
constexpr float kBelch = 70.0f;
constexpr int kBelchMillis = 300;
constexpr double kBelchGapSeconds = 0.25;
constexpr float kEquipment = 45.0f;
constexpr int kEquipmentMillis = 150;
// Landing from a fall of kLandingDrop or more: the bottom row, front and back, harder for a longer fall.
constexpr float kLandingLight = 35.0f;
constexpr float kLandingPerUnit = 6.0f;
constexpr float kLandingHard = 70.0f;
constexpr int kLandingMillis = 120;
// The portal's sweep: the band row at kPortalBandLow..High, about kPortalTrailShare of the row above it at
// kPortalTrailLow..High, a crackle of kPortalCrackleShare of the other rows at kPortalCrackleLow..High, and
// kPortalSleeveShare of the sleeves' motors at kPortalSleeveLow..High. Each step's frame lasts a little
// longer than a step, so the sweep does not stutter.
constexpr float kPortalBandLow = 55.0f;
constexpr float kPortalBandHigh = 85.0f;
constexpr std::uint32_t kPortalTrailShare = 50; // percent
constexpr float kPortalTrailLow = 20.0f;
constexpr float kPortalTrailHigh = 40.0f;
constexpr std::uint32_t kPortalCrackleShare = 20; // percent
constexpr float kPortalCrackleLow = 15.0f;
constexpr float kPortalCrackleHigh = 30.0f;
constexpr std::uint32_t kPortalSleeveShare = 50; // percent
constexpr float kPortalSleeveLow = 25.0f;
constexpr float kPortalSleeveHigh = 55.0f;
constexpr int kPortalMillis = 80;

// Column angles round the torso in degrees (0 ahead, positive to the wearer's left), wearer's left first.
constexpr std::array<float, kVestColumns> kFrontAngles{67.5f, 22.5f, -22.5f, -67.5f};
constexpr std::array<float, kVestColumns> kBackAngles{112.5f, 157.5f, -157.5f, -112.5f};
// A hit this far from a column's angle leaves it untouched.
constexpr float kHitReachDegrees = 67.5f;

float angleBetween(float a, float b) {
    float d = std::fmod(std::fabs(a - b), 360.0f);
    return d > 180.0f ? 360.0f - d : d;
}

std::uint8_t dotIndex(Device side, int wearerColumn, int row) {
    return static_cast<std::uint8_t>(row * kVestColumns + vestColumn(side, wearerColumn));
}

bool finite(float v) {
    return std::isfinite(v);
}

} // namespace

const char* positionName(Device device) {
    switch (device) {
    case Device::VestFront:
        return "VestFront";
    case Device::VestBack:
        return "VestBack";
    case Device::ForearmL:
        return "ForearmL";
    case Device::ForearmR:
        return "ForearmR";
    case Device::Count:
        break;
    }
    return "VestFront";
}

int motorCount(Device device) {
    return device == Device::VestFront || device == Device::VestBack ? kVestMotors : kSleeveMotors;
}

int vestColumn(Device side, int wearerColumn) {
    const int c = std::clamp(wearerColumn, 0, kVestColumns - 1);
    (void)side; // both sides count from the wearer's left
    return c;
}

const char* effectName(Effect effect) {
    switch (effect) {
    case Effect::Shot:
        return "shot";
    case Effect::Damage:
        return "damage";
    case Effect::Heartbeat:
        return "heartbeat";
    case Effect::GloryKill:
        return "glorykill";
    case Effect::Death:
        return "death";
    case Effect::Belch:
        return "belch";
    case Effect::Equipment:
        return "equipment";
    case Effect::Landing:
        return "landing";
    case Effect::Crystal:
        return "crystal";
    case Effect::Portal:
        return "portal";
    case Effect::Health:
        return "health";
    case Effect::MegaHealth:
        return "megahealth";
    case Effect::Armor:
        return "armor";
    case Effect::LargeArmor:
        return "largearmor";
    case Effect::Launch:
        return "launch";
    case Effect::Count:
        break;
    }
    return "shot";
}

SyncKind syncKindOf(std::string_view entityDefName) {
    if (entityDefName.find("argent_cell/use_sync") != std::string_view::npos) {
        return SyncKind::Crystal;
    }
    if (entityDefName.find("preator_suit_token") != std::string_view::npos ||
        entityDefName.find("praetor_suit_token") != std::string_view::npos) {
        return SyncKind::Token;
    }
    if (entityDefName.starts_with("interact/rune/")) {
        return SyncKind::Rune;
    }
    return entityDefName.starts_with("interact/") ? SyncKind::Pickup : SyncKind::GloryKill;
}

const char* syncKindName(SyncKind kind) {
    switch (kind) {
    case SyncKind::Pickup:
        return "pickup";
    case SyncKind::Crystal:
        return "Sentinel Crystal";
    case SyncKind::Token:
        return "Praetor token";
    case SyncKind::Rune:
        return "rune";
    case SyncKind::GloryKill:
        break;
    }
    return "glory kill";
}

TeleportKind teleportKindOf(bool fade, bool damage, std::string_view fadeSound, std::string_view entityName) {
    if (!fade) {
        return TeleportKind::Portal;
    }
    if (damage) {
        return TeleportKind::Hazard;
    }
    if (fadeSound.find("stinger_falling_damage") != std::string_view::npos &&
        entityName.find("secret") == std::string_view::npos) {
        return TeleportKind::OutOfBounds;
    }
    return TeleportKind::Portal;
}

const char* teleportKindName(TeleportKind kind) {
    switch (kind) {
    case TeleportKind::Hazard:
        return "hazard";
    case TeleportKind::OutOfBounds:
        return "out of bounds";
    case TeleportKind::Portal:
        break;
    }
    return "portal";
}

WeaponClass weaponClassOf(std::string_view declName) {
    const auto has = [declName](std::string_view part) {
        return declName.find(part) != std::string_view::npos;
    };
    if (has("bfg")) {
        return WeaponClass::Huge;
    }
    if (has("double_barrel") || has("super_shotgun") || has("rocket") || has("gauss") || has("ballista")) {
        return WeaponClass::Heavy;
    }
    if (has("plasma") || has("chaingun") || has("heavy_cannon") || has("heavy_rifle") || has("unmaykr") ||
        has("unmakyr")) {
        return WeaponClass::Light;
    }
    return WeaponClass::Medium;
}

const char* weaponClassName(WeaponClass weapon) {
    switch (weapon) {
    case WeaponClass::Light:
        return "light";
    case WeaponClass::Medium:
        return "medium";
    case WeaponClass::Heavy:
        return "heavy";
    case WeaponClass::Huge:
        return "huge";
    }
    return "medium";
}

std::vector<HitColumn> hitColumns(float yawDegrees) {
    std::vector<HitColumn> all;
    if (!finite(yawDegrees)) {
        return all;
    }
    for (int c = 0; c < kVestColumns; ++c) {
        all.push_back(
            {Device::VestFront, c, angleBetween(yawDegrees, kFrontAngles[static_cast<std::size_t>(c)])});
        all.push_back(
            {Device::VestBack, c, angleBetween(yawDegrees, kBackAngles[static_cast<std::size_t>(c)])});
    }
    // `weight` holds the distance until the two nearest are known.
    std::sort(all.begin(), all.end(),
              [](const HitColumn& a, const HitColumn& b) { return a.weight < b.weight; });
    all.resize(2);
    float strongest = 0.0f;
    for (HitColumn& column : all) {
        column.weight = std::max(0.0f, 1.0f - column.weight / kHitReachDegrees);
        strongest = std::max(strongest, column.weight);
    }
    for (HitColumn& column : all) {
        column.weight = strongest > 0.0f ? column.weight / strongest : 0.0f;
    }
    return all;
}

BodyHaptics::BodyHaptics(float strength)
    : strength_(finite(strength) && strength >= 0.0f && strength <= 1.0f ? strength : 1.0f) {}

void BodyHaptics::reset() {
    primed_ = false;
    crystalUntil_ = -1.0;
    portalUntil_ = -1.0;
    launchUntil_ = -1.0;
    recentHitYaw_.reset();
    dub_ = false;
    nextBeat_ = 0.0;
    healthWave_.stop();
    armorWave_.stop();
    vestHeldUntil_ = -1.0;
}

std::uint8_t BodyHaptics::scaled(float intensity) const {
    const float v = std::clamp(finite(intensity) ? intensity : 0.0f, 0.0f, 100.0f) * strength_;
    return static_cast<std::uint8_t>(std::lround(v));
}

void BodyHaptics::add(
    std::vector<Frame>& out, Effect effect, Device device, int millis, std::vector<Dot> dots) {
    std::erase_if(dots, [](const Dot& d) { return d.intensity == 0; });
    if (dots.empty()) {
        return;
    }
    out.push_back(Frame{effect, device, millis, std::move(dots)});
    ++counts_[static_cast<std::size_t>(effect)];
}

void BodyHaptics::shot(std::vector<Frame>& out, const BodySignals& signals) {
    if (signals.shots == 0 || signals.seconds < nextShot_) {
        return;
    }
    nextShot_ = signals.seconds + kShotGapSeconds;
    const Kick kick = kickOf(signals.weapon);
    const bool right = signals.weaponHand == input::Hand::Right;
    std::vector<Dot> sleeve;
    for (int i = 0; i < kSleeveMotors; ++i) {
        sleeve.push_back({static_cast<std::uint8_t>(i), scaled(kick.sleeve)});
    }
    add(out, Effect::Shot, right ? Device::ForearmR : Device::ForearmL, kick.millis, std::move(sleeve));
    // The upper chest on the weapon's side: the stock against the shoulder.
    const std::uint8_t chest = scaled(kick.sleeve * kick.chest);
    std::vector<Dot> front;
    for (const int wearerColumn : right ? std::array<int, 2>{2, 3} : std::array<int, 2>{0, 1}) {
        for (int row = 0; row < 2; ++row) {
            front.push_back({dotIndex(Device::VestFront, wearerColumn, row), chest});
        }
    }
    add(out, Effect::Shot, Device::VestFront, kick.millis, std::move(front));
}

void BodyHaptics::leftShoulder(std::vector<Frame>& out, Effect effect, float intensity, int millis) {
    for (const Device side : {Device::VestFront, Device::VestBack}) {
        std::vector<Dot> dots;
        for (int wearerColumn = 0; wearerColumn <= 1; ++wearerColumn) {
            for (int row = 0; row <= 1; ++row) {
                dots.push_back({dotIndex(side, wearerColumn, row), scaled(intensity)});
            }
        }
        add(out, effect, side, millis, std::move(dots));
    }
}

void BodyHaptics::damage(std::vector<Frame>& out, float amount, std::optional<float> yawDegrees) {
    const float base = std::min(100.0f, kDamageFloor + amount * kDamagePerPoint);
    std::array<std::vector<Dot>, 2> sides; // front, back
    const auto sideIndex = [](Device side) {
        return side == Device::VestFront ? 0u : 1u;
    };
    const std::vector<HitColumn> columns = yawDegrees ? hitColumns(*yawDegrees) : std::vector<HitColumn>{};
    if (!columns.empty()) {
        for (const HitColumn& column : columns) {
            for (int row = 1; row <= 3; ++row) {
                sides[sideIndex(column.side)].push_back(
                    {dotIndex(column.side, column.wearerColumn, row), scaled(base * column.weight)});
            }
        }
    } else {
        for (const Device side : {Device::VestFront, Device::VestBack}) {
            for (int wearerColumn = 1; wearerColumn <= 2; ++wearerColumn) {
                for (int row = 1; row <= 3; ++row) {
                    sides[sideIndex(side)].push_back(
                        {dotIndex(side, wearerColumn, row), scaled(base * kUndirectedShare)});
                }
            }
        }
    }
    add(out, Effect::Damage, Device::VestFront, kDamageMillis, std::move(sides[0]));
    add(out, Effect::Damage, Device::VestBack, kDamageMillis, std::move(sides[1]));
}

void BodyHaptics::landing(std::vector<Frame>& out, const BodySignals& signals) {
    const std::optional<Landing>& landed = signals.landing;
    if (!landed) {
        return;
    }
    lastLanding_ = landed;
    ++landings_;
    if (signals.dead || landed->drop < kLandingDrop) {
        return;
    }
    const float intensity =
        std::min(kLandingHard, kLandingLight + (landed->drop - kLandingDrop) * kLandingPerUnit);
    for (const Device side : {Device::VestFront, Device::VestBack}) {
        std::vector<Dot> dots;
        for (int wearerColumn = 0; wearerColumn < kVestColumns; ++wearerColumn) {
            dots.push_back({dotIndex(side, wearerColumn, kVestRows - 1), scaled(intensity)});
        }
        add(out, Effect::Landing, side, kLandingMillis, std::move(dots));
    }
}

std::uint32_t BodyHaptics::random() {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return random_;
}

float BodyHaptics::randomLevel(float low, float high) {
    return low + static_cast<float>(random() % 1000) / 1000.0f * (high - low);
}

void BodyHaptics::portal(std::vector<Frame>& out, double seconds) {
    if (seconds < nextPortal_ || seconds >= portalUntil_) {
        return;
    }
    nextPortal_ = std::max(nextPortal_ + kPortalStepSeconds, seconds);
    const double progress = std::clamp((seconds - portalStart_) / kPortalSeconds, 0.0, 1.0);
    const int band = std::min(kVestRows - 1, static_cast<int>(progress * kVestRows));
    for (const Device side : {Device::VestFront, Device::VestBack}) {
        std::vector<Dot> dots;
        for (int row = 0; row < kVestRows; ++row) {
            for (int wearerColumn = 0; wearerColumn < kVestColumns; ++wearerColumn) {
                float level = 0.0f;
                if (row == band) {
                    level = randomLevel(kPortalBandLow, kPortalBandHigh);
                } else if (row == band - 1) {
                    level = random() % 100 < kPortalTrailShare
                                ? randomLevel(kPortalTrailLow, kPortalTrailHigh)
                                : 0.0f;
                } else if (random() % 100 < kPortalCrackleShare) {
                    level = randomLevel(kPortalCrackleLow, kPortalCrackleHigh);
                }
                if (level > 0.0f) {
                    dots.push_back({dotIndex(side, wearerColumn, row), scaled(level)});
                }
            }
        }
        add(out, Effect::Portal, side, kPortalMillis, std::move(dots));
    }
    for (const Device sleeve : {Device::ForearmL, Device::ForearmR}) {
        std::vector<Dot> dots;
        for (int i = 0; i < kSleeveMotors; ++i) {
            if (random() % 100 < kPortalSleeveShare) {
                dots.push_back(
                    {static_cast<std::uint8_t>(i), scaled(randomLevel(kPortalSleeveLow, kPortalSleeveHigh))});
            }
        }
        add(out, Effect::Portal, sleeve, kPortalMillis, std::move(dots));
    }
}

void BodyHaptics::heartbeat(std::vector<Frame>& out, const BodySignals& signals) {
    if (signals.dead || !(signals.health > 0.0f && signals.health < kLowHealth)) {
        dub_ = false;
        nextBeat_ = 0.0;
        return;
    }
    if (signals.seconds < nextBeat_) {
        return;
    }
    const double share = std::clamp(static_cast<double>(signals.health / kLowHealth), 0.0, 1.0);
    const double period = kBeatFastSeconds + (kBeatSlowSeconds - kBeatFastSeconds) * share;
    const std::uint8_t level = scaled(dub_ ? kDub : kLub);
    // Left of the sternum, the wearer's inner left column.
    add(out, Effect::Heartbeat, Device::VestFront, kBeatMillis,
        {{dotIndex(Device::VestFront, 1, 1), level}, {dotIndex(Device::VestFront, 1, 2), level}});
    nextBeat_ = signals.seconds + (dub_ ? period - kDubSeconds : kDubSeconds);
    dub_ = !dub_;
}

std::vector<Frame> BodyHaptics::update(const BodySignals& signals) {
    std::vector<Frame> out;
    // The crystal's sync starts as its upgrade menu closes, before gameplay is back: its start is taken in
    // every update, and its wave timed from it below. A Praetor token's (no menu) the same way.
    const std::optional<double> waveDelay = signals.sync ? crystalWaveDelay(signals.syncKind) : std::nullopt;
    const bool crystalSync = waveDelay.has_value() && std::isfinite(signals.seconds);
    if (crystalSync && !crystalSync_) {
        crystalStart_ = signals.seconds;
        crystalDelay_ = *waveDelay;
        crystalWaves_ = crystalWaveCount(signals.syncKind);
        crystalPending_ = true;
    }
    crystalSync_ = crystalSync;
    crystalPending_ = crystalPending_ && crystalSync;
    const bool usable =
        signals.gameplay && finite(signals.health) && finite(signals.armor) && std::isfinite(signals.seconds);
    if (!usable || strength_ <= 0.0f) {
        reset();
        return out;
    }
    shot(out, signals);
    if (signals.belches > 0 && signals.seconds >= nextBelch_) {
        nextBelch_ = signals.seconds + kBelchGapSeconds;
        leftShoulder(out, Effect::Belch, kBelch, kBelchMillis);
    }
    if (signals.equipment > 0) {
        leftShoulder(out, Effect::Equipment, kEquipment, kEquipmentMillis);
    }
    landing(out, signals);
    if (signals.portals > 0 && !signals.dead && signals.seconds >= portalUntil_) {
        portalStart_ = signals.seconds;
        nextPortal_ = signals.seconds;
        portalUntil_ = signals.seconds + kPortalSeconds;
    }
    // One launch plays its curve to the end: another one meanwhile does not restart it.
    if (signals.launches > 0 && !signals.dead && signals.seconds >= launchUntil_) {
        launchStart_ = signals.seconds;
        launchNextStep_ = 0;
        launchUntil_ = signals.seconds + kLaunchSeconds;
    }
    if (primed_) {
        const float lost = std::max(0.0f, health_ - signals.health) + std::max(0.0f, armor_ - signals.armor);
        if (signals.hitSerial != hitSerial_) {
            recentHitYaw_ = signals.hitYawDegrees;
            recentHitSeconds_ = signals.seconds;
        }
        if (!dead_ && lost >= kMinDamage) {
            const bool recent = recentHitYaw_ && signals.seconds - recentHitSeconds_ <= kHitWindowSeconds;
            damage(out, lost, recent ? recentHitYaw_ : std::nullopt);
            recentHitYaw_.reset();
        }
        if (signals.sync && !sync_ && !signals.dead && signals.syncKind == SyncKind::GloryKill) {
            std::vector<Dot> front;
            for (int i = 0; i < kVestMotors; ++i) {
                front.push_back({static_cast<std::uint8_t>(i), scaled(kGloryKill)});
            }
            add(out, Effect::GloryKill, Device::VestFront, kGloryKillMillis, std::move(front));
            for (const Device sleeve : {Device::ForearmL, Device::ForearmR}) {
                std::vector<Dot> dots;
                for (int i = 0; i < kSleeveMotors; ++i) {
                    dots.push_back({static_cast<std::uint8_t>(i), scaled(kGloryKill)});
                }
                add(out, Effect::GloryKill, sleeve, kGloryKillMillis, std::move(dots));
            }
        }
        if (signals.dead && !dead_) {
            for (const Device side : {Device::VestFront, Device::VestBack}) {
                std::vector<Dot> dots;
                for (int i = 0; i < kVestMotors; ++i) {
                    dots.push_back({static_cast<std::uint8_t>(i), scaled(kDeath)});
                }
                add(out, Effect::Death, side, kDeathMillis, std::move(dots));
            }
        }
    }
    if (crystalPending_ && !signals.dead) {
        crystalPending_ = false;
        crystalWaveStart_ = crystalStart_ + crystalDelay_;
        nextCrystal_ = std::max(crystalWaveStart_, signals.seconds);
        crystalUntil_ = crystalWaveStart_ + crystalWaves_ * kCrystalWaveSeconds;
    }
    heartbeat(out, signals);
    crystal(out, signals.seconds);
    portal(out, signals.seconds);
    launch(out, signals.seconds);
    pickups(out, signals);
    primed_ = true;
    health_ = signals.health;
    armor_ = signals.armor;
    dead_ = signals.dead;
    sync_ = signals.sync;
    hitSerial_ = signals.hitSerial;
    return out;
}

} // namespace evr::bhaptics
