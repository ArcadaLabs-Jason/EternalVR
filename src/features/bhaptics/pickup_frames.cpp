// BodyHaptics::pickups: the pickups' waves (pickups.hpp's PickupWave) as frames on both sides of the vest.

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
    const bool held = signals.seconds < vestHeldUntil_;
    for (PickupWave* wave : {&healthWave_, &armorWave_}) {
        const std::optional<WaveStep> step = wave->step(signals.seconds);
        if (!step || held) {
            continue;
        }
        Effect effect = step->mega ? Effect::MegaHealth : Effect::Health;
        if (step->kind == PickupKind::Armor) {
            effect = Effect::Armor;
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
}

} // namespace evr::bhaptics
