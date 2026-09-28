#pragma once

// Builders for tracked input frames used across the input tests.

#include "common/pose.hpp"
#include "common/quat.hpp"
#include "features/input/controller_state.hpp"

namespace evr::test {

inline constexpr float kHeadHeight = 1.7f;

// A pose at `position` facing `yawRadians` counter-clockwise from -Z about +Y.
inline Pose yawPose(float yawRadians, Vec3 position = {}) {
    return {Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yawRadians), position};
}

// A pose at `position` pitched by `pitchRadians` about +X (positive looks up).
inline Pose pitchPose(float pitchRadians, Vec3 position = {}) {
    return {Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, pitchRadians), position};
}

inline input::HandState trackedHand(Vec3 position, Vec3 velocity = {}) {
    input::HandState hand;
    hand.poseValid = true;
    hand.aimPose = {Quat::identity(), position};
    hand.velocityValid = true;
    hand.linearVelocity = velocity;
    return hand;
}

// Head at standing height facing -Z, both hands tracked at chest height and at rest.
inline input::InputFrame restingFrame() {
    input::InputFrame frame;
    frame.head = {true, {Quat::identity(), {0.0f, kHeadHeight, 0.0f}}};
    frame.left = trackedHand({-0.2f, 1.2f, -0.3f});
    frame.right = trackedHand({0.2f, 1.2f, -0.3f});
    return frame;
}

} // namespace evr::test
