// The Sentinel Crystal's wave and BodyHaptics::crystal: the ring as frames on the vest and both sleeves.
// A Praetor Suit token's pickup and a rune's play it too (crystalWaveDelay, crystalWaveCount).

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

int crystalWaveCount(SyncKind kind) {
    return kind == SyncKind::Rune ? kRuneWaves : 1;
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

void BodyHaptics::crystal(std::vector<Frame>& out, double seconds) {
    if (seconds < nextCrystal_ || seconds >= crystalUntil_) {
        return;
    }
    nextCrystal_ = std::max(nextCrystal_ + kCrystalStepSeconds, seconds);
    // A late update plays the ring where it is by now, not the steps it missed. A rune's waves follow each
    // other, each from the centre again.
    const double sinceStart = std::fmod(seconds - crystalWaveStart_, kCrystalWaveSeconds);
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

} // namespace evr::bhaptics
