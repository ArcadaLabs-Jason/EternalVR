#include "features/input/usercmd_motion.hpp"

#include "features/input/input_frames.hpp"
#include "features/input/turn_policy.hpp"
#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <ostream>

using evr::Vec3;
using evr::input::AngleUnitAccumulator;
using evr::input::Axis2;
using evr::input::HandState;
using evr::input::HeadState;
using evr::input::kAngleUnitsPerDegree;
using evr::input::Locomotion;
using evr::input::LocomotionFrame;
using evr::input::MoveAxes;
using evr::input::moveInTrackingSpace;
using evr::input::quantizeMove;
using evr::input::TurnMode;
using evr::input::TurnPolicy;
using evr::input::TurnSettings;
using evr::test::approxEqual;
using evr::test::yawPose;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kFrame = 1.0f / 90.0f;
constexpr std::int32_t kFullTurnUnits = 65536;

HeadState headFacing(float yawRadians) {
    return {true, yawPose(yawRadians)};
}

HandState handPointing(float yawRadians) {
    HandState hand;
    hand.poseValid = true;
    hand.aimPose = yawPose(yawRadians);
    return hand;
}

// Turns with the stick held fully to one side until the policy has turned a whole circle, feeding the
// accumulator as the layer would. Returns the units handed out; `degrees` gets the policy's total.
std::int32_t turnFullCircle(TurnPolicy& policy, float stickX, float& degrees) {
    AngleUnitAccumulator accumulator;
    std::int32_t units = 0;
    degrees = 0.0f;
    for (int frame = 0; frame < 10000 && std::fabs(degrees) < 360.0f; ++frame) {
        const float remaining = 360.0f - std::fabs(degrees);
        float step = policy.update({stickX, 0.0f}, kFrame, true);
        // The last frame of a smooth turn is cut short, as the player lets go of the stick.
        if (std::fabs(step) > remaining) {
            step = std::copysign(remaining, step);
        }
        if (step == 0.0f) { // snap: centre the stick to re-arm
            policy.update({0.0f, 0.0f}, kFrame, true);
            continue;
        }
        degrees += step;
        units += accumulator.add(step);
    }
    units += accumulator.add(0.0f);
    return units;
}

} // namespace

TEST_CASE("move quantises to the command's axes and keeps the direction") {
    CHECK(quantizeMove({0.0f, 1.0f}, 127) == MoveAxes{127, 0});
    CHECK(quantizeMove({-1.0f, 0.0f}, 127) == MoveAxes{0, -127});
    CHECK(quantizeMove({0.5f, -0.25f}, 100) == MoveAxes{-25, 50});
    // A full diagonal is a unit diagonal, not the corner of the square.
    const MoveAxes diagonal = quantizeMove({1.0f, 1.0f}, 127);
    CHECK(diagonal.forward == 90);
    CHECK(diagonal.right == 90);
}

TEST_CASE("move quantisation rejects unusable input") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(quantizeMove({nan, 1.0f}, 127) == MoveAxes{});
    CHECK(quantizeMove({0.0f, 1.0f}, 0) == MoveAxes{});
}

TEST_CASE("the accumulator hands out whole units and carries the rest") {
    AngleUnitAccumulator accumulator;
    // 0.002 degrees is about 0.36 units: nothing yet, then the carry makes a unit.
    CHECK(accumulator.add(0.002f) == 0);
    CHECK(accumulator.add(0.002f) == 1);
    CHECK(std::fabs(accumulator.pendingDegrees()) <= 0.5f / kAngleUnitsPerDegree);
    accumulator.reset();
    CHECK(accumulator.pendingDegrees() == 0.0f);
    CHECK(accumulator.add(std::numeric_limits<float>::infinity()) == 0);
}

TEST_CASE("a turn in many uneven steps adds up to the exact units of the whole") {
    AngleUnitAccumulator accumulator;
    std::int32_t units = 0;
    float sent = 0.0f;
    for (int i = 0; i < 997; ++i) {
        const float step = 0.1f + 0.37f * static_cast<float>(i % 7) / 7.0f;
        units += accumulator.add(step);
        sent += step;
    }
    CHECK(std::fabs(static_cast<float>(units) / kAngleUnitsPerDegree - sent) < 0.05f);
    CHECK(std::fabs(accumulator.pendingDegrees()) <= 0.5f / kAngleUnitsPerDegree);
}

TEST_CASE("a 360-degree smooth turn by stick completes in both directions") {
    for (const float stickX : {1.0f, -1.0f}) {
        CAPTURE(stickX);
        TurnPolicy policy;
        float degrees = 0.0f;
        const std::int32_t units = turnFullCircle(policy, stickX, degrees);
        // Pushing right turns clockwise: negative yaw.
        CHECK(degrees == doctest::Approx(stickX > 0.0f ? -360.0f : 360.0f));
        CHECK(units == (stickX > 0.0f ? -kFullTurnUnits : kFullTurnUnits));
    }
}

TEST_CASE("a 360-degree turn at the slowest smooth rate and at partial deflection still completes") {
    TurnSettings settings;
    settings.smoothDegreesPerSecond = 150.0f;
    for (const float stickX : {0.6f, -0.6f}) {
        CAPTURE(stickX);
        TurnPolicy policy(settings);
        float degrees = 0.0f;
        const std::int32_t units = turnFullCircle(policy, stickX, degrees);
        CHECK(std::fabs(degrees) == doctest::Approx(360.0f));
        CHECK(std::abs(units) == kFullTurnUnits);
    }
}

TEST_CASE("a 360-degree snap turn completes in both directions for every offered angle") {
    for (const float snap : {30.0f, 45.0f, 90.0f}) {
        for (const float stickX : {1.0f, -1.0f}) {
            CAPTURE(snap);
            CAPTURE(stickX);
            TurnSettings settings;
            settings.mode = TurnMode::Snap;
            settings.snapDegrees = snap;
            TurnPolicy policy(settings);
            float degrees = 0.0f;
            const std::int32_t units = turnFullCircle(policy, stickX, degrees);
            CHECK(std::fabs(degrees) == doctest::Approx(360.0f));
            CHECK(units == (stickX > 0.0f ? -kFullTurnUnits : kFullTurnUnits));
        }
    }
}

TEST_CASE("head-relative locomotion moves where the head faces, whatever the view yaw") {
    Locomotion locomotion;
    const float headYaw = kPi / 2.0f; // facing left (-X)
    for (const float viewYaw : {0.0f, 1.0f, -2.5f}) {
        CAPTURE(viewYaw);
        const Axis2 move = locomotion.update({0.0f, 1.0f}, LocomotionFrame::Head, headFacing(headYaw),
                                             handPointing(0.0f), viewYaw);
        CHECK(magnitude(move) == doctest::Approx(1.0f));
        // The game applies the move in its view frame; in tracking space that must be the head's forward.
        CHECK(approxEqual(moveInTrackingSpace(move, viewYaw), Vec3{-1.0f, 0.0f, 0.0f}, 1e-5f));
    }
}

TEST_CASE("hand-relative locomotion moves where the off hand points") {
    Locomotion locomotion;
    const float handYaw = -kPi / 2.0f; // pointing right (+X)
    const Axis2 move = locomotion.update({0.0f, 1.0f}, LocomotionFrame::OffHand, headFacing(0.0f),
                                         handPointing(handYaw), 0.3f);
    CHECK(locomotion.lastYaw() == doctest::Approx(handYaw));
    CHECK(approxEqual(moveInTrackingSpace(move, 0.3f), Vec3{1.0f, 0.0f, 0.0f}, 1e-5f));
    // Strafing right from a hand pointing right goes backwards (+Z).
    const Axis2 strafe = locomotion.update({1.0f, 0.0f}, LocomotionFrame::OffHand, headFacing(0.0f),
                                           handPointing(handYaw), 0.3f);
    CHECK(approxEqual(moveInTrackingSpace(strafe, 0.3f), Vec3{0.0f, 0.0f, 1.0f}, 1e-5f));
}

TEST_CASE("locomotion applies the move stick's deadzone and never exceeds full speed") {
    Locomotion locomotion;
    const Axis2 rest =
        locomotion.update({0.05f, 0.05f}, LocomotionFrame::Head, headFacing(0.0f), handPointing(0.0f), 0.0f);
    CHECK(magnitude(rest) == 0.0f);
    const Axis2 corner =
        locomotion.update({1.0f, 1.0f}, LocomotionFrame::Head, headFacing(0.0f), handPointing(0.0f), 0.0f);
    CHECK(magnitude(corner) <= 1.0f + 1e-6f);
    const Axis2 nan = locomotion.update({0.0f, 1.0f}, LocomotionFrame::Head, headFacing(0.0f),
                                        handPointing(0.0f), std::numeric_limits<float>::quiet_NaN());
    CHECK(nan == Axis2{});
}
