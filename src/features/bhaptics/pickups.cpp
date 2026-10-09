#include "features/bhaptics/pickups.hpp"

#include <algorithm>
#include <cmath>

namespace evr::bhaptics {

const char* pickupKindName(PickupKind kind) {
    return kind == PickupKind::Armor ? "armor" : "health";
}

const char* pickupSkipName(PickupSkip skip) {
    switch (skip) {
    case PickupSkip::Settling:
        return "just after the start, a load or a respawn";
    case PickupSkip::Dead:
        return "dead";
    case PickupSkip::FromZero:
        return "from 0: a respawn or an extra life";
    case PickupSkip::None:
        break;
    }
    return "felt";
}

void PickupDetector::reset() {
    primed_ = false;
    lastSeconds_ = 0.0;
    settleUntil_ = 0.0;
    dead_ = false;
    health_ = Track{};
    armor_ = Track{};
}

void PickupDetector::step(Track& track,
                          PickupKind kind,
                          float value,
                          double seconds,
                          PickupSkip skip,
                          bool fromZeroSkips,
                          std::vector<Pickup>& out) {
    // A gain ends before a later rise is looked at: that one starts the next.
    if (track.pending && (seconds - track.lastRise >= kPickupMergeSeconds ||
                          seconds - track.firstRise >= kPickupMaxMergeSeconds)) {
        track.pending = false;
        Pickup& gain = track.gain;
        gain.spanSeconds = track.lastRise - track.firstRise;
        gain.mega =
            kind == PickupKind::Health ? gain.amount >= kMegaHealthGain : gain.biggestStep > kLargeArmorStep;
        if (gain.amount >= kMinPickup) {
            out.push_back(gain);
        }
    }
    const float before = track.value;
    track.value = value;
    const float rise = value - before;
    if (!(rise >= kPickupStepNoise)) {
        return;
    }
    if (!track.pending) {
        track.pending = true;
        track.firstRise = seconds;
        track.gain = Pickup{};
        track.gain.kind = kind;
        track.gain.from = before;
        track.gain.skip =
            skip == PickupSkip::None && fromZeroSkips && before <= 0.0f ? PickupSkip::FromZero : skip;
    }
    // A death or a respawn inside the gain takes it all.
    if (track.gain.skip == PickupSkip::None) {
        track.gain.skip = skip;
    }
    // The highest value reached: a hit inside the gain takes nothing from it, and jitter adds up to nothing.
    track.gain.amount = std::max(track.gain.amount, value - track.gain.from);
    track.gain.biggestStep = std::max(track.gain.biggestStep, rise);
    track.gain.to = value;
    ++track.gain.steps;
    track.lastRise = seconds;
}

std::vector<Pickup> PickupDetector::update(const PickupReading& reading) {
    std::vector<Pickup> out;
    if (!reading.inPlay || !std::isfinite(reading.health) || !std::isfinite(reading.armor) ||
        !std::isfinite(reading.seconds)) {
        reset();
        return out;
    }
    if (primed_ && reading.seconds <= lastSeconds_) {
        return out; // the same game frame again
    }
    if (!primed_ || reading.seconds - lastSeconds_ > kPickupReadingGapSeconds) {
        reset();
        primed_ = true;
        lastSeconds_ = reading.seconds;
        settleUntil_ = reading.seconds + kPickupSettleSeconds;
        dead_ = reading.dead;
        health_.value = reading.health;
        armor_.value = reading.armor;
        return out;
    }
    lastSeconds_ = reading.seconds;
    if (dead_ && !reading.dead) {
        settleUntil_ = reading.seconds + kPickupSettleSeconds; // back from death
    }
    dead_ = reading.dead;
    PickupSkip skip = PickupSkip::None;
    if (reading.dead) {
        skip = PickupSkip::Dead;
    } else if (reading.seconds < settleUntil_) {
        skip = PickupSkip::Settling;
    }
    const bool healthWasZero = health_.value <= 0.0f;
    step(health_, PickupKind::Health, reading.health, reading.seconds, skip, true, out);
    // Armor from 0 is a pickup, unless health comes back from 0 with it (a respawn, an extra life).
    const bool healthFromZero = healthWasZero || reading.health <= 0.0f ||
                                (health_.pending && health_.gain.skip == PickupSkip::FromZero);
    step(armor_, PickupKind::Armor, reading.armor, reading.seconds, skip, healthFromZero, out);
    return out;
}

void addPickup(std::optional<Pickup>& pending, const Pickup& pickup) {
    if (!pending) {
        pending = pickup;
        return;
    }
    pending->amount += pickup.amount;
    pending->to = pickup.to;
    pending->steps += pickup.steps;
    pending->biggestStep = std::max(pending->biggestStep, pickup.biggestStep);
    pending->mega = pending->mega || pickup.mega;
}

float pickupIntensity(const Pickup& pickup) {
    if (pickup.mega) {
        return kMegaHealthIntensity;
    }
    const float amount = std::isfinite(pickup.amount) ? std::max(0.0f, pickup.amount) : 0.0f;
    return std::min(kPickupStrong, kPickupLight + amount * kPickupPerPoint);
}

PickupWave::PickupWave(int rows) : rows_(std::max(1, rows)) {}

void PickupWave::start(const Pickup& pickup, double seconds) {
    active_ = true;
    kind_ = pickup.kind;
    mega_ = pickup.mega;
    start_ = seconds;
    rowSeconds_ = pickup.mega ? kPickupRowSeconds * kMegaHealthSlower : kPickupRowSeconds;
    intensity_ = pickupIntensity(pickup);
    done_ = -1;
}

std::optional<WaveStep> PickupWave::step(double seconds) {
    if (!active_) {
        return std::nullopt;
    }
    const int i = static_cast<int>(std::max(0.0, seconds - start_) / rowSeconds_);
    if (i >= rows_) {
        active_ = false;
        return std::nullopt;
    }
    if (i <= done_) {
        return std::nullopt;
    }
    done_ = i;
    // Health goes up the vest from the bottom row, armor down it from the top.
    const bool up = kind_ == PickupKind::Health;
    WaveStep s;
    s.kind = kind_;
    s.mega = mega_;
    s.row = up ? rows_ - 1 - i : i;
    s.trailRow = i == 0 ? -1 : (up ? s.row + 1 : s.row - 1);
    s.intensity = intensity_;
    s.millis = static_cast<int>(std::lround(rowSeconds_ * 1000.0)) + kPickupOverlapMillis;
    return s;
}

} // namespace evr::bhaptics
