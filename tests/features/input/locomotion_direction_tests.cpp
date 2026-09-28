#include "features/input/locomotion_direction.hpp"

#include "features/input/input_frames.hpp"

#include <doctest/doctest.h>

#include <numbers>
#include <ostream>

using evr::input::Axis2;
using evr::input::HandState;
using evr::input::HeadState;
using evr::input::horizontalYaw;
using evr::input::LocomotionDirection;
using evr::input::LocomotionFrame;
using evr::input::rotateIntoViewFrame;
using evr::test::pitchPose;
using evr::test::yawPose;

namespace {

constexpr float kQuarterTurn = std::numbers::pi_v<float> / 2.0f;
// Outside the range horizontalYaw returns, so a missing yaw fails the comparison.
constexpr float kNoYaw = 100.0f;

HeadState headFacing(float yaw) {
    return {true, yawPose(yaw)};
}

HandState handPointing(const evr::Pose& pose) {
    HandState hand;
    hand.poseValid = true;
    hand.aimPose = pose;
    return hand;
}

} // namespace

TEST_CASE("horizontal yaw of a level pose is its heading") {
    CHECK(horizontalYaw(yawPose(0.0f)).value_or(kNoYaw) == doctest::Approx(0.0f));
    CHECK(horizontalYaw(yawPose(kQuarterTurn)).value_or(kNoYaw) == doctest::Approx(kQuarterTurn));
    CHECK(horizontalYaw(yawPose(-2.0f)).value_or(kNoYaw) == doctest::Approx(-2.0f));
}

TEST_CASE("pitch does not change the horizontal yaw") {
    const evr::Pose pitchedThenTurned = {yawPose(1.0f).orientation * pitchPose(0.8f).orientation, {}};
    CHECK(horizontalYaw(pitchedThenTurned).value_or(kNoYaw) == doctest::Approx(1.0f));
}

TEST_CASE("near-vertical directions have no horizontal yaw") {
    CHECK_FALSE(horizontalYaw(pitchPose(-1.5f)).has_value());
    CHECK_FALSE(horizontalYaw(pitchPose(1.5f)).has_value());
}

TEST_CASE("head frame follows the head") {
    LocomotionDirection direction;
    CHECK(direction.update(LocomotionFrame::Head, headFacing(0.5f), handPointing(yawPose(-1.0f))) ==
          doctest::Approx(0.5f));
}

TEST_CASE("off-hand frame follows the off hand") {
    LocomotionDirection direction;
    CHECK(direction.update(LocomotionFrame::OffHand, headFacing(0.5f), handPointing(yawPose(-1.0f))) ==
          doctest::Approx(-1.0f));
}

TEST_CASE("off-hand frame falls back to the head when the hand is untracked") {
    LocomotionDirection direction;
    HandState lost = handPointing(yawPose(-1.0f));
    lost.poseValid = false;
    CHECK(direction.update(LocomotionFrame::OffHand, headFacing(0.5f), lost) == doctest::Approx(0.5f));
}

TEST_CASE("off-hand frame falls back to the head when the hand points at the floor") {
    // A seated player's off hand resting in the lap.
    LocomotionDirection direction;
    CHECK(direction.update(LocomotionFrame::OffHand, headFacing(0.5f), handPointing(pitchPose(-1.4f))) ==
          doctest::Approx(0.5f));
}

TEST_CASE("the last good yaw is kept while no direction is usable") {
    LocomotionDirection direction;
    direction.update(LocomotionFrame::Head, headFacing(0.7f), {});
    CHECK(direction.update(LocomotionFrame::Head, {true, pitchPose(-1.5f)}, {}) == doctest::Approx(0.7f));
    CHECK(direction.update(LocomotionFrame::Head, {}, {}) == doctest::Approx(0.7f));
}

TEST_CASE("move is unchanged when locomotion and view agree") {
    const Axis2 move = rotateIntoViewFrame({0.3f, 0.8f}, 1.2f, 1.2f);
    CHECK(move.x == doctest::Approx(0.3f));
    CHECK(move.y == doctest::Approx(0.8f));
}

TEST_CASE("forward follows the head when the gun points elsewhere") {
    // Head faces 90 degrees left of the gun: pushing forward must move left in the gun's frame.
    const Axis2 forward = rotateIntoViewFrame({0.0f, 1.0f}, kQuarterTurn, 0.0f);
    CHECK(forward.x == doctest::Approx(-1.0f));
    CHECK(forward.y == doctest::Approx(0.0f).epsilon(1e-5));

    // Strafing right relative to that head is forward for the gun.
    const Axis2 right = rotateIntoViewFrame({1.0f, 0.0f}, kQuarterTurn, 0.0f);
    CHECK(right.x == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(right.y == doctest::Approx(1.0f));
}
