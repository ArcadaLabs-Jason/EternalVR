// bHaptics suits and sleeves (docs/BHAPTICS.md, features/bhaptics/body_haptics.hpp).
//
// The game threads only read and note: the fire hook counts the local player's shots and the held
// weapon's class (noteBhapticsShot), the camera hook reads the player's health, armor, death, sync kill and
// newest hit once a game frame (noteBhapticsFrame). A thread of its own, started once with the game hooks
// when ETERNALVR_BHAPTICS=1, takes those every kTickMs, runs the effects and submits their frames to the
// bHaptics Player's WebSocket on this PC (bhaptics_link.hpp). With no Player listening each try fails at
// once and the next waits kRetrySeconds; nothing is kept for later, so nothing plays late.
//
// Layout facts are from the type-info tables of Steam build 25216728 (docs/rig-findings/engine-facts.md),
// used only where PlayerAim confirmed that build (isPlayerSafe):
// - idPlayer::playerHealth (idPlayerHealth) at +0x37218: components[2] (0xB0 bytes each) from +0x10,
//   health then armor, each one's `cur` float at +0x34; isDead (bool) at +0x1B0.
// - idPlayer::damageFeedbackComponent (idDamageFeedbackComponent) at +0x26CD8: damageFeedback, 10 items
//   of 0x80 bytes from +0x88, each with damage (float, +0x0), selfDamage (+0x28), impactDir (idVec3,
//   +0x38) and addedTimeStamp (+0x70); damageFeedbackBufferPos (int) at +0x588.
// - idPlayer::savedSyncEntity's object (+0x8428): the sync entity of the animation the player is in, from its
//   start to its end. Traced in headset sessions (2026-09-30): a glory kill sets it to syncmelee/<demon>
//   (syncmelee/imp, syncmelee/zombie_tier1, ...) for about 1.6 s, a Sentinel Crystal's upgrade to
//   interact/argent_cell/use_sync for about 3.3 s, other pickups to interact/... (a mod bot about 2.8 s; a
//   Praetor Suit token interact/preator_suit_token/preator_suit_token_sync for about 3.1 s, on the rig).
//   idPlayer::syncMaster's object (+0x7DB0) never changed in those sessions and is only a fallback. The sync
//   entity's idEntity::entityDef at +0xA8 names which kind it is.
// Health and armor pickups are found here too (features/bhaptics/pickups.hpp), on every game frame.
// Portals come from two hooks of their own (bhaptics_portal.cpp), jump pads and boosters from two more
// (bhaptics_launch.cpp), installed with the thread.
// - idHavokPhysics_Player::viewAngles (+0x8A50 + 0x3F10), the yaw in degrees at +4 (as viewmodel_hook.cpp).
// Which hit is the newest, and that impactDir points from the attacker to the player, are read from the
// names; the log's first hits show the raw values so a rig session can confirm them.

#include "vkcore/controllers_impl.hpp"

#include "features/bhaptics/bhaptics_message.hpp"
#include "features/bhaptics/body_haptics.hpp"
#include "vkcore/bhaptics_link.hpp"
#include "vkcore/body_follow.hpp"
#include "vkcore/log.hpp"
#include "vkcore/menu_input.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "bhaptics";

constexpr std::size_t kPlayerHealth = 0x37218;
constexpr std::size_t kHealthComponents = 0x10;
constexpr std::size_t kHealthComponentSize = 0xB0;
constexpr std::size_t kComponentCur = 0x34;
constexpr std::size_t kHealthIsDead = 0x1B0;
constexpr std::size_t kPlayerDamageFeedback = 0x26CD8;
constexpr std::size_t kFeedbackItems = 0x88;
constexpr std::size_t kFeedbackItemSize = 0x80;
constexpr std::size_t kFeedbackItemCount = 10;
constexpr std::size_t kFeedbackPos = 0x588;
constexpr std::size_t kItemDamage = 0x0;
constexpr std::size_t kItemSelfDamage = 0x28;
constexpr std::size_t kItemImpactDir = 0x38;
constexpr std::size_t kItemAddedTime = 0x70;
constexpr std::size_t kPlayerViewYaw = 0x8A50 + 0x3F10 + 4;

// Readings outside this are not health.
constexpr float kMaxPlausible = 10000.0f;
// A camera-hook reading older than this is not gameplay (a load, the game stalled).
constexpr double kFrameStaleSeconds = 0.25;
constexpr int kTickMs = 20;
constexpr double kRetrySeconds = 3.0;
constexpr ULONGLONG kSummaryTicks = 30000;
constexpr int kLoggedHits = 20;
constexpr int kLoggedLandings = 30;
constexpr int kLoggedSyncs = 40;
constexpr int kLoggedPickups = 60;
constexpr int kLoggedSkippedPickups = 20;
constexpr float kMinImpactDir = 1e-3f;

// What the game threads noted, under g_mutex.
struct Noted {
    bool valid = false; // the last camera-hook reading succeeded
    LONGLONG qpc = 0;
    bool gameplay = false;
    float health = 0.0f;
    float armor = 0.0f;
    bool dead = false;
    bool sync = false;
    bhaptics::SyncKind syncKind = bhaptics::SyncKind::GloryKill;
    std::uint32_t hitSerial = 0;
    std::optional<float> hitYaw;
    // The newest hit's raw values, for the log.
    std::size_t hitItem = 0;
    std::int32_t hitPos = 0;
    std::int64_t hitTime = 0;
    float hitDamage = 0.0f;
    std::array<float, 3> hitDir{};
    float viewYaw = 0.0f;
};

std::mutex g_mutex;
Noted g_noted;
std::atomic<std::uint32_t> g_shots{0};
std::atomic<std::uint32_t> g_belches{0};
std::atomic<std::uint32_t> g_portals{0};
std::atomic<std::uint32_t> g_launches{0};
std::atomic<std::uint8_t> g_weapon{static_cast<std::uint8_t>(bhaptics::WeaponClass::Medium)};
std::atomic<bool> g_running{false};
std::once_flag g_startOnce;
// The fire hook's cache of the held weapon's decl.
std::atomic<const std::byte*> g_weaponDecl{nullptr};
std::atomic<int> g_loggedWeapons{0};
// Camera hook only.
int g_loggedHits = 0;
std::uint32_t g_lastHitSerial = 0;
bool g_loggedFirstRead = false;
// The sync entity last seen and what it is (read once when it appears).
const std::byte* g_syncObject = nullptr;
bhaptics::SyncKind g_syncKind = bhaptics::SyncKind::GloryKill;
LONGLONG g_syncStart = 0;
int g_loggedSyncs = 0;
// Landings, found on every game frame (the link thread's own ticks can miss a whole fall while it waits
// for the Player): the detector is the camera hook's, the largest landing not yet taken is under g_mutex.
bhaptics::LandingDetector g_feet;
std::optional<bhaptics::Landing> g_landing;
// Pickups, found on every game frame for the same reason: the detector is the camera hook's, the gains not
// yet taken are under g_mutex.
bhaptics::PickupDetector g_pickups;
std::optional<bhaptics::Pickup> g_healthGain;
std::optional<bhaptics::Pickup> g_armorGain;
int g_loggedPickups = 0;
int g_loggedSkippedPickups = 0;

double qpcSeconds(LONGLONG qpc) {
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    return static_cast<double>(qpc) / frequency;
}

double seconds() {
    return qpcSeconds(nowQpc());
}

float wrapDegrees(float d) {
    d = std::fmod(d + 180.0f, 360.0f);
    return (d < 0.0f ? d + 360.0f : d) - 180.0f;
}

// The newest hit in the damage feedback buffer: the one added last, or when no item carries a time, the one
// before the buffer's position. Its yaw from the player's facing when it has a direction.
void readNewestHit(const std::byte* player, Noted& n) {
    const std::byte* feedback = player + kPlayerDamageFeedback;
    std::int32_t pos = 0;
    if (!safeRead(feedback + kFeedbackPos, pos)) {
        return;
    }
    std::size_t newest = (static_cast<std::size_t>(pos) + kFeedbackItemCount - 1) % kFeedbackItemCount;
    std::int64_t newestTime = 0;
    for (std::size_t i = 0; i < kFeedbackItemCount; ++i) {
        std::int64_t t = 0;
        if (safeRead(feedback + kFeedbackItems + i * kFeedbackItemSize + kItemAddedTime, t) &&
            t > newestTime) {
            newestTime = t;
            newest = i;
        }
    }
    const std::byte* item = feedback + kFeedbackItems + newest * kFeedbackItemSize;
    bool self = false;
    if (!safeCopy(n.hitDir.data(), item + kItemImpactDir, sizeof(float) * 3) ||
        !safeRead(item + kItemSelfDamage, self) || !safeRead(item + kItemDamage, n.hitDamage)) {
        return;
    }
    n.hitItem = newest;
    n.hitPos = pos;
    n.hitTime = newestTime;
    n.hitSerial = static_cast<std::uint32_t>(newestTime) ^ (static_cast<std::uint32_t>(pos) << 24);
    const float length = std::hypot(n.hitDir[0], n.hitDir[1]);
    if (self || !(length > kMinImpactDir) || !std::isfinite(length) ||
        !safeRead(player + kPlayerViewYaw, n.viewYaw) || !std::isfinite(n.viewYaw)) {
        return;
    }
    // The hit travels along impactDir, so it came from the opposite way. id Tech yaw: +x ahead at 0, +y
    // (left) at 90.
    const float from = std::atan2(-n.hitDir[1], -n.hitDir[0]) * 180.0f / std::numbers::pi_v<float>;
    n.hitYaw = wrapDegrees(from - n.viewYaw);
}

// The entityDef name of a game entity, or empty.
std::string entityDefName(const std::byte* entity) {
    const std::byte* def = nullptr;
    return safeRead(entity + kEntityDef, def) && def ? itemDeclName(def) : std::string{};
}

bool readPlayer(const std::byte* player, Noted& n) {
    const std::byte* health = player + kPlayerHealth;
    const std::byte* components = health + kHealthComponents;
    bool dead = false;
    if (!safeRead(components + kComponentCur, n.health) ||
        !safeRead(components + kHealthComponentSize + kComponentCur, n.armor) ||
        !safeRead(health + kHealthIsDead, dead)) {
        return false;
    }
    n.dead = dead;
    if (!std::isfinite(n.health) || !std::isfinite(n.armor) || std::fabs(n.health) > kMaxPlausible ||
        std::fabs(n.armor) > kMaxPlausible) {
        return false;
    }
    const std::byte* master = nullptr;
    const std::byte* saved = nullptr;
    if (!safeRead(player + kPlayerSyncMaster, master)) {
        master = nullptr;
    }
    if (!safeRead(player + kPlayerSavedSync, saved)) {
        saved = nullptr;
    }
    const std::byte* sync = saved ? saved : master;
    n.sync = sync != nullptr;
    if (n.sync && sync != g_syncObject) {
        const std::string name = entityDefName(sync);
        g_syncKind = bhaptics::syncKindOf(name);
        g_syncStart = n.qpc;
        if (g_loggedSyncs++ < kLoggedSyncs) {
            EVR_LOG("%s: sync starts: '%s' (%s)", kTag, name.c_str(), bhaptics::syncKindName(g_syncKind));
        }
    }
    if (!n.sync && g_syncObject && g_loggedSyncs <= kLoggedSyncs) {
        EVR_LOG("%s: sync ends after %.2f s (%s)", kTag, qpcSeconds(n.qpc) - qpcSeconds(g_syncStart),
                bhaptics::syncKindName(g_syncKind));
    }
    g_syncObject = n.sync ? sync : nullptr;
    n.syncKind = g_syncKind;
    readNewestHit(player, n);
    return true;
}

void logPickup(const bhaptics::Pickup& p) {
    const bool felt = p.skip == bhaptics::PickupSkip::None;
    if (felt ? g_loggedPickups++ >= kLoggedPickups : g_loggedSkippedPickups++ >= kLoggedSkippedPickups) {
        return;
    }
    const char* mega = p.kind != bhaptics::PickupKind::Health ? "" : p.mega ? " (mega yes)" : " (mega no)";
    if (felt) {
        EVR_LOG("%s: pickup: %s +%.3g%s, %.1f to %.1f in %d step%s over %.2f s", kTag,
                bhaptics::pickupKindName(p.kind), p.amount, mega, p.from, p.to, p.steps,
                p.steps == 1 ? "" : "s", p.spanSeconds);
    } else {
        EVR_LOG("%s: rise not felt (%s): %s +%.3g, %.1f to %.1f in %d step%s over %.2f s", kTag,
                bhaptics::pickupSkipName(p.skip), bhaptics::pickupKindName(p.kind), p.amount, p.from, p.to,
                p.steps, p.steps == 1 ? "" : "s", p.spanSeconds);
    }
}

void logSummary(const bhaptics::BodyHaptics& body,
                std::uint64_t sent,
                std::uint64_t failed,
                ULONGLONG& last,
                std::uint64_t& loggedTotal) {
    const ULONGLONG now = GetTickCount64();
    if (now - last < kSummaryTicks) {
        return;
    }
    last = now;
    std::uint64_t total = 0;
    for (const auto c : body.counts()) {
        total += c;
    }
    if (total == loggedTotal) {
        return;
    }
    loggedTotal = total;
    const auto& c = body.counts();
    EVR_LOG(
        "%s: %llu frames (shot %llu, damage %llu, heartbeat %llu, glory kill %llu, death %llu, belch %llu, "
        "equipment %llu, landing %llu, crystal %llu, portal %llu, health %llu, mega health %llu, "
        "armor %llu, launch %llu), %llu messages sent, %llu failed",
        kTag, static_cast<unsigned long long>(total), static_cast<unsigned long long>(c[0]),
        static_cast<unsigned long long>(c[1]), static_cast<unsigned long long>(c[2]),
        static_cast<unsigned long long>(c[3]), static_cast<unsigned long long>(c[4]),
        static_cast<unsigned long long>(c[5]), static_cast<unsigned long long>(c[6]),
        static_cast<unsigned long long>(c[7]), static_cast<unsigned long long>(c[8]),
        static_cast<unsigned long long>(c[9]), static_cast<unsigned long long>(c[10]),
        static_cast<unsigned long long>(c[11]), static_cast<unsigned long long>(c[12]),
        static_cast<unsigned long long>(c[13]), static_cast<unsigned long long>(sent),
        static_cast<unsigned long long>(failed));
}

void linkMain(float intensity) {
    bhaptics::BodyHaptics body(intensity);
    WebSocketLink link;
    const std::string path = bhaptics::feedbackPath();
    double nextTry = 0.0;
    std::uint64_t tries = 0;
    std::uint64_t sent = 0;
    std::uint64_t failed = 0;
    bool loggedReply = false;
    bool equipmentWasHeld = false;
    ULONGLONG lastSummary = GetTickCount64();
    std::uint64_t loggedTotal = 0;
    std::uint64_t landingsSeen = 0;
    int loggedLandings = 0;
    for (;;) {
        Sleep(kTickMs);
        const double now = seconds();
        if (!link.isOpen() && now >= nextTry) {
            std::string error;
            ++tries;
            if (link.open(bhaptics::kPlayerPort, path, error)) {
                EVR_LOG("%s: connected to the bHaptics Player (ws://127.0.0.1:%d%s)", kTag,
                        bhaptics::kPlayerPort, path.c_str());
                loggedReply = false;
                body.reset();
            } else {
                nextTry = now + kRetrySeconds;
                // The first failure, then one line a minute or so while it keeps failing.
                if (tries == 1 || tries % 20 == 0) {
                    EVR_LOG("%s: no bHaptics Player on port %d (%s); trying again every %.0f s", kTag,
                            bhaptics::kPlayerPort, error.c_str(), kRetrySeconds);
                }
            }
        }
        if (link.isOpen() && !loggedReply) {
            const std::string reply = link.firstReply();
            if (!reply.empty()) {
                loggedReply = true;
                EVR_LOG("%s: the Player says: %s", kTag, reply.c_str());
            }
        }
        bhaptics::BodySignals signals;
        Noted noted;
        std::optional<bhaptics::Landing> landing;
        {
            std::lock_guard lock(g_mutex);
            noted = g_noted;
            landing = std::exchange(g_landing, std::nullopt);
            signals.healthGain = std::exchange(g_healthGain, std::nullopt);
            signals.armorGain = std::exchange(g_armorGain, std::nullopt);
        }
        signals.seconds = now;
        signals.gameplay =
            noted.valid && noted.gameplay && noted.qpc && secondsSince(noted.qpc) < kFrameStaleSeconds;
        signals.health = noted.health;
        signals.armor = noted.armor;
        signals.dead = noted.dead;
        signals.sync = noted.sync;
        signals.syncKind = noted.syncKind;
        signals.hitSerial = noted.hitSerial;
        signals.hitYawDegrees = noted.hitYaw;
        signals.shots = g_shots.exchange(0);
        signals.belches = g_belches.exchange(0);
        signals.portals = g_portals.exchange(0);
        signals.launches = g_launches.exchange(0);
        // The equipment launcher: its button going down (the fire hook does not see it).
        const bool equipmentHeld = game::contains(heldActions(), game::GameAction::Equipment);
        signals.equipment = equipmentHeld && !equipmentWasHeld ? 1 : 0;
        equipmentWasHeld = equipmentHeld;
        signals.weapon = static_cast<bhaptics::WeaponClass>(g_weapon.load());
        signals.weaponHand = weaponHand();
        signals.landing = landing;
        const std::vector<bhaptics::Frame> frames = body.update(signals);
        if (body.landings() != landingsSeen) {
            landingsSeen = body.landings();
            const std::optional<bhaptics::Landing> l = body.lastLanding();
            if (l && loggedLandings++ < kLoggedLandings) {
                EVR_LOG("%s: landing: dropped %.2f units, fastest fall %.1f units/s (%s from %.1f)", kTag,
                        l->drop, l->fallSpeed, l->drop >= bhaptics::kLandingDrop ? "felt" : "not felt, felt",
                        bhaptics::kLandingDrop);
            }
        }
        if (frames.empty() || !link.isOpen()) {
            logSummary(body, sent, failed, lastSummary, loggedTotal);
            continue;
        }
        std::string error;
        if (link.sendText(bhaptics::submitMessage(frames), error)) {
            ++sent;
        } else {
            ++failed;
            nextTry = now + kRetrySeconds;
            EVR_LOG("%s: lost the bHaptics Player (%s); trying again every %.0f s", kTag, error.c_str(),
                    kRetrySeconds);
        }
        logSummary(body, sent, failed, lastSummary, loggedTotal);
    }
}

} // namespace

void startBhaptics() {
    const input::ControllerSettings& cfg = settings();
    if (!cfg.bhaptics) {
        return;
    }
    std::call_once(g_startOnce, [&cfg] {
        std::string error;
        if (!WebSocketLink::available(error)) {
            EVR_LOG("%s: off: %s", kTag, error.c_str());
            return;
        }
        if (!state().player.available()) {
            // The shots come from the fire hook, which needs the same build.
            EVR_LOG("%s: off: unknown game build, nothing to read", kTag);
            return;
        }
        EVR_LOG(
            "%s: on, intensity %.2f (shot, damage, heartbeat, glory kill, death, belch, equipment, "
            "landing, crystal, portal, health and armor pickups, launch); looking for the bHaptics Player on "
            "port %d",
            kTag, cfg.bhapticsIntensity, bhaptics::kPlayerPort);
        g_running.store(true);
        std::thread(linkMain, cfg.bhapticsIntensity).detach();
        if (!installBhapticsPortalHooks()) {
            EVR_LOG("%s: no portal hooks: going through a portal plays nothing", kTag);
        }
        if (!installBhapticsLaunchHooks()) {
            EVR_LOG("%s: no launch hooks: jump pads and boosters play nothing", kTag);
        }
    });
}

void noteBhapticsShot(const std::byte* hands) {
    if (!g_running.load(std::memory_order_relaxed)) {
        return;
    }
    if (game::contains(heldActions(), game::GameAction::FlameBelch)) {
        // The Flame Belch fires through the same hook, with the held weapon's decl.
        g_belches.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_shots.fetch_add(1, std::memory_order_relaxed);
    const std::byte* decl = heldItemDecl(hands);
    if (g_weaponDecl.exchange(decl, std::memory_order_relaxed) == decl) {
        return;
    }
    const std::string name = decl ? itemDeclName(decl) : std::string{};
    const bhaptics::WeaponClass weapon = bhaptics::weaponClassOf(name);
    g_weapon.store(static_cast<std::uint8_t>(weapon));
    if (g_loggedWeapons.fetch_add(1) < 40) {
        EVR_LOG("%s: firing '%s': %s kick", kTag, name.c_str(), bhaptics::weaponClassName(weapon));
    }
}

void noteBhapticsPortal() {
    if (g_running.load(std::memory_order_relaxed)) {
        g_portals.fetch_add(1, std::memory_order_relaxed);
    }
}

void noteBhapticsLaunch() {
    if (g_running.load(std::memory_order_relaxed)) {
        g_launches.fetch_add(1, std::memory_order_relaxed);
    }
}

void noteBhapticsFrame(const std::byte* player) {
    if (!g_running.load(std::memory_order_relaxed)) {
        return;
    }
    Noted n;
    n.qpc = nowQpc();
    n.valid = isPlayerSafe(player) && readPlayer(player, n);
    // Cutscenes count: a scripted sync kill is felt like any other.
    n.gameplay = !menu_input::suppressGameplay();
    std::optional<bhaptics::Landing> landed;
    const std::optional<float> feet = n.valid && n.gameplay ? body_follow::feetHeight(player) : std::nullopt;
    if (feet) {
        landed = g_feet.update(*feet, qpcSeconds(n.qpc));
    } else {
        g_feet.reset();
    }
    const std::vector<bhaptics::Pickup> pickups =
        g_pickups.update({qpcSeconds(n.qpc), n.valid && n.gameplay, n.health, n.armor, n.dead});
    for (const bhaptics::Pickup& p : pickups) {
        logPickup(p);
    }
    if (n.valid && !g_loggedFirstRead) {
        g_loggedFirstRead = true;
        g_lastHitSerial = n.hitSerial;
        EVR_LOG("%s: first player reading: health %.1f, armor %.1f, dead %d, sync %d", kTag, n.health,
                n.armor, n.dead ? 1 : 0, n.sync ? 1 : 0);
    }
    if (n.valid && n.hitSerial != g_lastHitSerial) {
        g_lastHitSerial = n.hitSerial;
        if (g_loggedHits++ < kLoggedHits) {
            EVR_LOG(
                "%s: new hit: item %zu (buffer position %d, time %lld), damage %.1f, impact direction (%.2f "
                "%.2f %.2f), view yaw %.1f, from %s%.0f deg (0 ahead, 90 left); health %.1f, armor %.1f",
                kTag, n.hitItem, n.hitPos, static_cast<long long>(n.hitTime), n.hitDamage, n.hitDir[0],
                n.hitDir[1], n.hitDir[2], n.viewYaw, n.hitYaw ? "" : "(no direction) ",
                n.hitYaw ? *n.hitYaw : 0.0f, n.health, n.armor);
        }
    }
    std::lock_guard lock(g_mutex);
    g_noted = n;
    if (landed && (!g_landing || landed->drop > g_landing->drop)) {
        g_landing = landed;
    }
    for (const bhaptics::Pickup& p : pickups) {
        if (p.skip == bhaptics::PickupSkip::None) {
            bhaptics::addPickup(p.kind == bhaptics::PickupKind::Armor ? g_armorGain : g_healthGain, p);
        }
    }
}

} // namespace evr::vkcore::controllers
