#pragma once

// One frame of tracked input as plain values (ARCHITECTURE section 9).
//
// The OpenXR side fills these from its action states and space locations; nothing here knows about
// OpenXR, so every mapping policy can be tested on any machine. Poses and velocities are all in the
// one tracking space the frame loop uses, with the usual convention (+Y up, -Z forward).

#include "common/pose.hpp"
#include "common/vector.hpp"
#include "features/input/axis2.hpp"

#include <cstdint>

namespace evr::input {

enum class Hand : std::uint8_t {
    Left,
    Right,
};

constexpr Hand otherHand(Hand hand) {
    return hand == Hand::Left ? Hand::Right : Hand::Left;
}

struct HandState {
    float trigger = 0.0f; // 0..1
    float grip = 0.0f;    // 0..1
    Axis2 stick;
    bool stickClick = false;
    bool primaryButton = false;   // A on the right Touch controller, X on the left.
    bool secondaryButton = false; // B on the right, Y on the left.
    bool menuButton = false;

    // The aim pose: -Z runs along the controller's pointing ray.
    bool poseValid = false;
    Pose aimPose;

    // Linear velocity of the controller in metres per second.
    bool velocityValid = false;
    Vec3 linearVelocity;
};

struct HeadState {
    bool poseValid = false;
    Pose pose;
};

struct InputFrame {
    HandState left;
    HandState right;
    HeadState head;
    // The room transform the poses above were located with (LOCAL to room space; identity without a
    // recenter), so a pose can be taken back into LOCAL.
    Pose roomFromLocal;

    [[nodiscard]] const HandState& hand(Hand which) const { return which == Hand::Left ? left : right; }
};

} // namespace evr::input
