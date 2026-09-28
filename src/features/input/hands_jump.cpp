#include "features/input/hands_jump.hpp"

#include <algorithm>
#include <cmath>

namespace evr::input {

namespace {

bool handTracked(const HandState& hand) {
    return hand.poseValid && hand.velocityValid && std::isfinite(hand.linearVelocity.y) &&
           std::isfinite(hand.aimPose.position.y);
}

} // namespace

bool HandsJumpDetector::update(const InputFrame& frame, posture::Posture posture, float dtSeconds) {
    if (!active(frame, posture)) {
        reset();
        return false;
    }

    const float leftUp = frame.left.linearVelocity.y;
    const float rightUp = frame.right.linearVelocity.y;
    cooldown_ = std::max(0.0f, cooldown_ - dtSeconds);
    sinceLeftFast_ = leftUp >= settings_.minUpwardSpeed ? 0.0f : sinceLeftFast_ + dtSeconds;
    sinceRightFast_ = rightUp >= settings_.minUpwardSpeed ? 0.0f : sinceRightFast_ + dtSeconds;
    leftSeenFast_ = leftSeenFast_ || leftUp >= settings_.minUpwardSpeed;
    rightSeenFast_ = rightSeenFast_ || rightUp >= settings_.minUpwardSpeed;

    if (leftUp <= settings_.settleSpeed && rightUp <= settings_.settleSpeed) {
        armed_ = true;
    }

    const float headHeight = frame.head.pose.position.y + settings_.minHeightAboveHead;
    const bool bothHigh =
        frame.left.aimPose.position.y >= headHeight && frame.right.aimPose.position.y >= headHeight;
    const bool bothFast = leftSeenFast_ && rightSeenFast_ && sinceLeftFast_ <= settings_.fastWindowSeconds &&
                          sinceRightFast_ <= settings_.fastWindowSeconds;
    if (!armed_ || cooldown_ > 0.0f || !bothHigh || !bothFast) {
        return false;
    }
    armed_ = false;
    cooldown_ = settings_.cooldownSeconds;
    return true;
}

bool HandsJumpDetector::active(const InputFrame& frame, posture::Posture posture) const {
    if (!settings_.enabled) {
        return false;
    }
    if (posture == posture::Posture::Seated && !settings_.allowWhenSeated) {
        return false;
    }
    return frame.head.poseValid && std::isfinite(frame.head.pose.position.y) && handTracked(frame.left) &&
           handTracked(frame.right);
}

void HandsJumpDetector::reset() {
    armed_ = false;
    cooldown_ = 0.0f;
    sinceLeftFast_ = 0.0f;
    sinceRightFast_ = 0.0f;
    leftSeenFast_ = false;
    rightSeenFast_ = false;
}

} // namespace evr::input
