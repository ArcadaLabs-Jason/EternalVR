#include "features/comfort/launch_pulse.hpp"

#include "common/finite.hpp"

#include <algorithm>
#include <cmath>

namespace evr::comfort {

namespace {

// A little past the pad's flight time: the landing ends the hold anyway.
constexpr float kFlightMargin = 0.25f;

float smoothstep01(float t) {
    const float x = std::clamp(t, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

LaunchPulseTiming sanitized(LaunchPulseTiming t) {
    const LaunchPulseTiming d;
    t.riseSeconds = finiteInRangeOr(t.riseSeconds, 0.0f, 2.0f, d.riseSeconds);
    t.minHoldSeconds = finiteInRangeOr(t.minHoldSeconds, 0.0f, 5.0f, d.minHoldSeconds);
    t.fallSeconds = finiteInRangeOr(t.fallSeconds, 0.0f, 5.0f, d.fallSeconds);
    t.leaveGroundSeconds = finiteInRangeOr(t.leaveGroundSeconds, 0.0f, 5.0f, d.leaveGroundSeconds);
    return t;
}

} // namespace

float launchHoldSeconds(const LaunchSize& size) {
    float hold = kDefaultLaunchHoldSeconds;
    if (std::isfinite(size.flightSeconds) && size.flightSeconds > 0.0f) {
        hold = size.flightSeconds + kFlightMargin;
    } else if (std::isfinite(size.speed) && size.speed > 0.0f) {
        hold = 2.0f * size.speed / kLaunchGravity;
    }
    return std::clamp(hold, kMinLaunchHoldSeconds, kMaxLaunchHoldSeconds);
}

LaunchPulse::LaunchPulse(LaunchPulseTiming timing) : timing_(sanitized(timing)) {}

void LaunchPulse::launch(double now, float holdCapSeconds) {
    if (!std::isfinite(now)) {
        return;
    }
    const float from = amount(now);
    const float cap = std::isfinite(holdCapSeconds) ? holdCapSeconds : kDefaultLaunchHoldSeconds;
    started_ = true;
    start_ = now;
    from_ = from;
    holdCap_ = std::max(cap, timing_.minHoldSeconds);
    leftGround_ = false;
    landed_ = false;
    landedAt_ = 0.0;
}

void LaunchPulse::ground(double now, bool onGround) {
    if (!started_ || landed_ || !std::isfinite(now) || now < start_) {
        return;
    }
    if (!onGround) {
        leftGround_ = true;
        return;
    }
    if (leftGround_ || now - start_ >= timing_.leaveGroundSeconds) {
        landed_ = true;
        landedAt_ = now;
    }
}

double LaunchPulse::holdEnd() const {
    const double cap = start_ + holdCap_;
    const double least = start_ + std::max(timing_.minHoldSeconds, timing_.riseSeconds);
    const double end = landed_ ? std::min(landedAt_, cap) : cap;
    return std::max(end, least);
}

float LaunchPulse::amount(double now) const {
    if (!started_ || !std::isfinite(now) || now < start_) {
        return 0.0f;
    }
    const double t = now - start_;
    if (t < timing_.riseSeconds) {
        const float rise = smoothstep01(static_cast<float>(t / timing_.riseSeconds));
        return from_ + (1.0f - from_) * rise;
    }
    const double end = holdEnd();
    if (now < end) {
        return 1.0f;
    }
    if (timing_.fallSeconds <= 0.0f) {
        return 0.0f;
    }
    const float f = static_cast<float>((now - end) / timing_.fallSeconds);
    return f >= 1.0f ? 0.0f : 1.0f - smoothstep01(f);
}

bool LaunchPulse::active(double now) const {
    return started_ && std::isfinite(now) && now >= start_ && now < holdEnd() + timing_.fallSeconds;
}

void LaunchPulse::reset() {
    *this = LaunchPulse(timing_);
}

bool SlamWatch::update(const HammerReading& r) {
    if (!r.valid) {
        have_ = false;
        return false;
    }
    if (!r.held) {
        return false;
    }
    // Only a later time is a slam: a lower one (a load restarting the game's clock) is a new base.
    const bool slam = have_ && r.slamStartTime > last_ && r.slamStartTime != 0;
    have_ = true;
    last_ = r.slamStartTime;
    return slam;
}

} // namespace evr::comfort
