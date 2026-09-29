#include "features/input/punch_detector.hpp"

#include "common/finite.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace evr::input {

namespace {

PunchSettings sanitized(PunchSettings settings) {
    const PunchSettings defaults;
    const float threshold = std::isfinite(settings.thresholdMetresPerSecond)
                                ? settings.thresholdMetresPerSecond
                                : defaults.thresholdMetresPerSecond;
    settings.thresholdMetresPerSecond =
        std::clamp(threshold, kMinPunchMetresPerSecond, kMaxPunchMetresPerSecond);
    // Above 1 the hand would re-arm while still punching and fire again on the next frame.
    settings.rearmFraction = finiteInRangeOr(settings.rearmFraction, 0.0f, 1.0f, defaults.rearmFraction);
    return settings;
}

} // namespace

PunchDetector::PunchDetector(PunchSettings settings) : settings_(sanitized(settings)) {}

bool PunchDetector::update(const InputFrame& frame, const std::array<bool, 2>& heldBack) {
    punched_ = {};
    if (!settings_.enabled || !frame.head.poseValid) {
        armed_ = {};
        return false;
    }
    const Vec3 headForward = transformDirection(frame.head.pose, {0.0f, 0.0f, -1.0f});
    for (std::size_t i = 0; i < heldBack.size(); ++i) {
        if (heldBack[i]) {
            armed_[i] = false;
        }
    }
    punched_[0] = !heldBack[0] && updateHand(frame.left, headForward, armed_[0]);
    punched_[1] = !heldBack[1] && updateHand(frame.right, headForward, armed_[1]);
    return punched_[0] || punched_[1];
}

bool PunchDetector::updateHand(const HandState& hand, Vec3 headForward, bool& armed) const {
    if (!hand.velocityValid) {
        armed = false;
        return false;
    }
    const float forwardSpeed = dot(hand.linearVelocity, headForward);
    if (!std::isfinite(forwardSpeed)) {
        armed = false;
        return false;
    }
    if (forwardSpeed < settings_.thresholdMetresPerSecond * settings_.rearmFraction) {
        armed = true;
        return false;
    }
    if (!armed || forwardSpeed < settings_.thresholdMetresPerSecond) {
        return false;
    }
    armed = false;
    return true;
}

} // namespace evr::input
