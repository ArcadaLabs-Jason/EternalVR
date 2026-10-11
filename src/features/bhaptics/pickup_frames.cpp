// BodyHaptics::pickups: the pickups' waves (pickups.hpp's PickupWave) as frames on both sides of the vest,
// and the ammo's (ammo.hpp's AmmoWave) on its back.

#include "features/bhaptics/body_haptics.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace evr::bhaptics {

namespace {

// The effects a pickup's wave gives way to.
bool outranksPickups(Effect effect) {
    switch (effect) {
    case Effect::Damage:
    case Effect::GloryKill:
    case Effect::Death:
    case Effect::Landing:
    case Effect::Crystal:
    case Effect::Portal:
    case Effect::Launch:
        return true;
    default:
        return false;
    }
}

std::uint8_t dot(Device side, int wearerColumn, int row) {
    return static_cast<std::uint8_t>(row * kVestColumns + vestColumn(side, wearerColumn));
}

} // namespace

const char* ammoFeltName(AmmoFelt felt) {
    switch (felt) {
    case AmmoFelt::Wave:
        return "a wave down the back";
    case AmmoFelt::Merged:
        return "merged into the last wave";
    case AmmoFelt::NotFelt:
        break;
    }
    return "not felt (dead or a Sentinel Crystal)";
}

void BodyHaptics::pickups(std::vector<Frame>& out, const BodySignals& signals) {
    for (const Frame& frame : out) {
        if (outranksPickups(frame.effect)) {
            vestHeldUntil_ = std::max(vestHeldUntil_, signals.seconds + frame.durationMillis / 1000.0);
        }
    }
    // A Sentinel Crystal's upgrade may fill health or armor: its wave stands for that.
    const bool crystal = signals.sync && signals.syncKind == SyncKind::Crystal;
    if (signals.healthGain && !signals.dead && !crystal) {
        healthWave_.start(*signals.healthGain, signals.seconds);
    }
    if (signals.armorGain && !signals.dead && !crystal) {
        armorWave_.start(*signals.armorGain, signals.seconds);
    }
    if (signals.ammo > 0) {
        ammoPickups_ += signals.ammo;
        if (signals.dead || crystal) {
            lastAmmo_ = AmmoFelt::NotFelt;
        } else {
            lastAmmo_ = ammoWave_.pickUp(signals.seconds) ? AmmoFelt::Wave : AmmoFelt::Merged;
        }
    }
    const bool held = signals.seconds < vestHeldUntil_;
    // The ammo's wave gives way to health and armor too: theirs cover the back as well.
    const bool waves = healthWave_.active() || armorWave_.active();
    for (PickupWave* wave : {&healthWave_, &armorWave_}) {
        const std::optional<WaveStep> step = wave->step(signals.seconds);
        if (!step || held) {
            continue;
        }
        Effect effect = step->mega ? Effect::MegaHealth : Effect::Health;
        if (step->kind == PickupKind::Armor) {
            effect = step->mega ? Effect::LargeArmor : Effect::Armor;
        }
        for (const Device side : {Device::VestFront, Device::VestBack}) {
            std::vector<Dot> dots;
            for (int wearerColumn = 0; wearerColumn < kVestColumns; ++wearerColumn) {
                dots.push_back({dot(side, wearerColumn, step->row), scaled(step->intensity)});
                if (step->trailRow >= 0) {
                    dots.push_back({dot(side, wearerColumn, step->trailRow),
                                    scaled(step->intensity * kPickupTrailShare)});
                }
            }
            add(out, effect, side, step->millis, std::move(dots));
        }
    }
    const std::optional<AmmoStep> ammo = ammoWave_.step(signals.seconds);
    if (!ammo || held || waves) {
        return;
    }
    std::vector<Dot> dots;
    for (int wearerColumn = 0; wearerColumn < kVestColumns; ++wearerColumn) {
        dots.push_back({dot(Device::VestBack, wearerColumn, ammo->row), scaled(ammo->intensity)});
        if (ammo->trailRow >= 0) {
            dots.push_back({dot(Device::VestBack, wearerColumn, ammo->trailRow),
                            scaled(ammo->intensity * kAmmoTrailShare)});
        }
    }
    add(out, Effect::Ammo, Device::VestBack, ammo->millis, std::move(dots));
}

} // namespace evr::bhaptics
