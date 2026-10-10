// The Sentinel Crystal's wave and BodyHaptics::crystal: the ring as frames on the vest and both sleeves.
// A Praetor Suit token's pickup plays it too; a rune's plays BodyHaptics::runeShock (crystalWaveDelay,
// crystalEffectSeconds).

#include "features/bhaptics/crystal.hpp"

#include "features/bhaptics/body_haptics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace evr::bhaptics {

namespace {

// The ring's peak on the vest and on the sleeves (lighter as the charge leaves), each dot at kCrystalFlicker
// to 1 of it at random, and each step's frame a little longer than a step, so the wave does not stutter.
constexpr float kCrystalPeak = 85.0f;
constexpr float kCrystalSleevePeak = 70.0f;
constexpr float kCrystalFlicker = 0.75f;
constexpr int kCrystalMillis = 100;
// A rune's sparks: at most these on the vest and the sleeves, each at kRuneSparkLow to 1 of it at random.
constexpr float kRunePeak = 80.0f;
constexpr float kRuneSleevePeak = 70.0f;
constexpr float kRuneSparkLow = 0.5f;

// The ring's radius at the end: its tail has just left the sleeves.
constexpr float kCrystalEndRadius = kCrystalSleeveDistance + kCrystalTrail;

} // namespace

std::optional<double> crystalWaveDelay(SyncKind kind) {
    switch (kind) {
    case SyncKind::Crystal:
        return kCrystalDelaySeconds;
    case SyncKind::Token:
        return kTokenDelaySeconds;
    case SyncKind::Rune:
        return kRuneDelaySeconds;
    case SyncKind::GloryKill:
    case SyncKind::Pickup:
        break;
    }
    return std::nullopt;
}

double crystalEffectSeconds(SyncKind kind) {
    return kind == SyncKind::Rune ? kRuneShockSeconds : kCrystalWaveSeconds;
}

float crystalDistance(int row, int column) {
    const float down = static_cast<float>(row) - static_cast<float>(kVestRows - 1) / 2.0f;
    const float across = static_cast<float>(column) - static_cast<float>(kVestColumns - 1) / 2.0f;
    return std::sqrt(down * down + across * across);
}

float crystalWaveShare(double sinceStart, float distance) {
    if (!std::isfinite(sinceStart) || !std::isfinite(distance) || sinceStart < 0.0 ||
        sinceStart >= kCrystalWaveSeconds) {
        return 0.0f;
    }
    const auto progress = static_cast<float>(sinceStart / kCrystalWaveSeconds);
    const float radius = kCrystalStartRadius + progress * (kCrystalEndRadius - kCrystalStartRadius);
    const float share = distance > radius ? 1.0f - (distance - radius) / kCrystalLead
                                          : 1.0f - (radius - distance) / kCrystalTrail;
    return std::max(0.0f, share);
}

float runeShockShare(double sinceStart) {
    if (!std::isfinite(sinceStart) || sinceStart < 0.0 || sinceStart >= kRuneShockSeconds) {
        return 0.0f;
    }
    const double rise = 0.3 + 0.7 * sinceStart / kRuneShockRiseSeconds;
    const double fade = (kRuneShockSeconds - sinceStart) / kRuneShockFadeSeconds;
    return static_cast<float>(std::min({1.0, rise, fade}));
}

void BodyHaptics::crystal(std::vector<Frame>& out, double seconds) {
    if (seconds < nextCrystal_ || seconds >= crystalUntil_) {
        return;
    }
    nextCrystal_ = std::max(nextCrystal_ + kCrystalStepSeconds, seconds);
    // A late update plays the ring where it is by now, not the steps it missed.
    const double sinceStart = seconds - crystalWaveStart_;
    if (crystalRune_) {
        runeShock(out, sinceStart);
        return;
    }
    for (const Device side : {Device::VestFront, Device::VestBack}) {
        std::vector<Dot> dots;
        for (int row = 0; row < kVestRows; ++row) {
            for (int wearerColumn = 0; wearerColumn < kVestColumns; ++wearerColumn) {
                const float share = crystalWaveShare(sinceStart, crystalDistance(row, wearerColumn));
                if (share > 0.0f) {
                    dots.push_back(
                        {static_cast<std::uint8_t>(row * kVestColumns + vestColumn(side, wearerColumn)),
                         scaled(kCrystalPeak * share * randomLevel(kCrystalFlicker, 1.0f))});
                }
            }
        }
        add(out, Effect::Crystal, side, kCrystalMillis, std::move(dots));
    }
    const float sleeveShare = crystalWaveShare(sinceStart, kCrystalSleeveDistance);
    if (sleeveShare <= 0.0f) {
        return;
    }
    for (const Device sleeve : {Device::ForearmL, Device::ForearmR}) {
        std::vector<Dot> dots;
        for (int i = 0; i < kSleeveMotors; ++i) {
            dots.push_back({static_cast<std::uint8_t>(i),
                            scaled(kCrystalSleevePeak * sleeveShare * randomLevel(kCrystalFlicker, 1.0f))});
        }
        add(out, Effect::Crystal, sleeve, kCrystalMillis, std::move(dots));
    }
}

void BodyHaptics::runeShock(std::vector<Frame>& out, double sinceStart) {
    const float share = runeShockShare(sinceStart);
    if (share <= 0.0f) {
        return;
    }
    const auto sparks = [&](Device device, int motors, float peak) {
        std::vector<Dot> dots;
        for (int i = 0; i < motors; ++i) {
            if (randomLevel(0.0f, 1.0f) < kRuneSparkShare) {
                dots.push_back(
                    {static_cast<std::uint8_t>(i), scaled(peak * share * randomLevel(kRuneSparkLow, 1.0f))});
            }
        }
        add(out, Effect::Crystal, device, kCrystalMillis, std::move(dots));
    };
    sparks(Device::VestFront, kVestMotors, kRunePeak);
    sparks(Device::VestBack, kVestMotors, kRunePeak);
    sparks(Device::ForearmL, kSleeveMotors, kRuneSleevePeak);
    sparks(Device::ForearmR, kSleeveMotors, kRuneSleevePeak);
}

} // namespace evr::bhaptics
