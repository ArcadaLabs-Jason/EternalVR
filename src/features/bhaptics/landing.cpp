#include "features/bhaptics/landing.hpp"

#include <algorithm>
#include <cmath>

namespace evr::bhaptics {

void LandingDetector::reset() {
    lastHeight_.reset();
    lastSeconds_ = 0.0;
    airborne_ = false;
    falling_ = false;
    peak_ = 0.0f;
    fastestFall_ = 0.0f;
    restSince_.reset();
}

std::optional<Landing> LandingDetector::update(float height, double seconds) {
    if (!std::isfinite(height) || !std::isfinite(seconds)) {
        reset();
        return std::nullopt;
    }
    if (lastHeight_ && seconds <= lastSeconds_) {
        return std::nullopt; // the same game frame again
    }
    if (!lastHeight_ || seconds - lastSeconds_ > kMaxReadingGapSeconds) {
        reset();
        lastHeight_ = height;
        lastSeconds_ = seconds;
        return std::nullopt;
    }
    const float before = *lastHeight_;
    const float speed = (height - before) / static_cast<float>(seconds - lastSeconds_);
    lastHeight_ = height;
    lastSeconds_ = seconds;
    if (std::fabs(speed) > kTeleportSpeed) {
        airborne_ = false;
        falling_ = false;
        restSince_.reset();
        return std::nullopt;
    }
    if (!airborne_) {
        if (std::fabs(speed) > kAirborneSpeed) {
            airborne_ = true;
            falling_ = speed < 0.0f;
            peak_ = std::max(before, height);
            fastestFall_ = std::max(0.0f, -speed);
            restSince_.reset();
        }
        return std::nullopt;
    }
    peak_ = std::max(peak_, height);
    fastestFall_ = std::max(fastestFall_, -speed);
    if (speed < -kAirborneSpeed) {
        falling_ = true;
    } else if (speed > kAirborneSpeed) {
        falling_ = false; // a double jump, or pushed up again
    }
    if (std::fabs(speed) > kRestSpeed) {
        restSince_.reset();
        return std::nullopt;
    }
    if (falling_) {
        airborne_ = false;
        falling_ = false;
        restSince_.reset();
        return Landing{std::max(0.0f, peak_ - height), fastestFall_};
    }
    if (!restSince_) {
        restSince_ = seconds;
    } else if (seconds - *restSince_ >= kQuietRestSeconds) {
        airborne_ = false;
        restSince_.reset();
    }
    return std::nullopt;
}

} // namespace evr::bhaptics
