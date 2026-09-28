#include "features/input/locomotion_direction.hpp"

#include <cmath>

namespace evr::input {

std::optional<float> horizontalYaw(const Pose& pose, float minHorizontal) {
    const Vec3 forward = transformDirection(pose, {0.0f, 0.0f, -1.0f});
    if (!std::isfinite(forward.x) || !std::isfinite(forward.z)) {
        return std::nullopt;
    }
    if (std::hypot(forward.x, forward.z) < minHorizontal) {
        return std::nullopt;
    }
    return std::atan2(-forward.x, -forward.z);
}

float LocomotionDirection::update(LocomotionFrame frame, const HeadState& head, const HandState& offHand) {
    if (frame == LocomotionFrame::OffHand && offHand.poseValid) {
        if (const auto yaw = horizontalYaw(offHand.aimPose)) {
            lastYaw_ = *yaw;
            return lastYaw_;
        }
    }
    if (head.poseValid) {
        if (const auto yaw = horizontalYaw(head.pose)) {
            lastYaw_ = *yaw;
        }
    }
    return lastYaw_;
}

Axis2 rotateIntoViewFrame(Axis2 move, float locomotionYaw, float viewYaw) {
    // Right and forward for yaw a are (cos a, -sin a) and (-sin a, -cos a) in (x, z); projecting the
    // locomotion-frame vector onto the view frame's axes gives a plain 2D rotation by the difference.
    const float delta = locomotionYaw - viewYaw;
    const float c = std::cos(delta);
    const float s = std::sin(delta);
    return {move.x * c - move.y * s, move.x * s + move.y * c};
}

} // namespace evr::input
