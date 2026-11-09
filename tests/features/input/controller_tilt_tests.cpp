#include "features/input/controller_tilt.hpp"

#include "common/pose.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

using evr::compose;
using evr::inverse;
using evr::Pose;
using evr::Quat;
using evr::transformDirection;
using evr::Vec3;
using evr::input::controllerTilt;
using evr::input::fitScriptedHands;
using evr::input::Hand;
using evr::input::HandState;
using evr::input::InputFrame;
using evr::input::tiltController;

namespace {

constexpr float kRadians = std::numbers::pi_v<float> / 180.0f;

void checkVec(Vec3 a, Vec3 b) {
    CHECK(a.x == doctest::Approx(b.x).epsilon(1e-4));
    CHECK(a.y == doctest::Approx(b.y).epsilon(1e-4));
    CHECK(a.z == doctest::Approx(b.z).epsilon(1e-4));
}

// A hand turned and moved somewhere in the room, its aim pose 2 cm ahead of the grip and pitched 40 degrees
// down from it (roughly a Valve Index's).
HandState someHand() {
    HandState h;
    h.gripValid = true;
    h.gripPose = {Quat::fromAxisAngle({0.3f, 1.0f, 0.2f}, 50.0f * kRadians), Vec3{0.2f, 1.1f, -0.4f}};
    h.poseValid = true;
    const Pose aimInGrip{Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, -40.0f * kRadians),
                         Vec3{0.0f, 0.0f, -0.02f}};
    h.aimPose = compose(h.gripPose, aimInGrip);
    return h;
}

} // namespace

TEST_CASE("a positive angle lifts the controller's front about the grip's sideways axis") {
    HandState h;
    h.gripValid = true;
    tiltController(h, 30.0f);
    // The grip's forward (-Z) now points 30 degrees up, its sideways axis (+X) where it was.
    checkVec(transformDirection(h.gripPose, {0.0f, 0.0f, -1.0f}),
             {0.0f, std::sin(30.0f * kRadians), -std::cos(30.0f * kRadians)});
    checkVec(transformDirection(h.gripPose, {1.0f, 0.0f, 0.0f}), {1.0f, 0.0f, 0.0f});
    HandState down;
    down.gripValid = true;
    tiltController(down, -40.0f);
    CHECK(transformDirection(down.gripPose, {0.0f, 0.0f, -1.0f}).y < -0.6f);
}

TEST_CASE("grip and aim each turn about their own centre, keeping their angle to each other") {
    HandState h = someHand();
    const HandState before = h;
    tiltController(h, -25.0f);
    checkVec(h.gripPose.position, before.gripPose.position);
    checkVec(h.aimPose.position, before.aimPose.position);
    // The aim's sideways axis is the grip's, so the aim keeps its angle to the grip.
    const Pose relBefore = compose(inverse(before.gripPose), before.aimPose);
    const Pose relAfter = compose(inverse(h.gripPose), h.aimPose);
    checkVec(transformDirection(relAfter, {0.0f, 0.0f, -1.0f}),
             transformDirection(relBefore, {0.0f, 0.0f, -1.0f}));
    // And each turned by exactly the angle about its own x axis.
    for (const auto& [was, now] :
         {std::pair{before.gripPose, h.gripPose}, std::pair{before.aimPose, h.aimPose}}) {
        const Pose turn = compose(inverse(was), now);
        checkVec(transformDirection(turn, {0.0f, 0.0f, -1.0f}),
                 {0.0f, std::sin(-25.0f * kRadians), -std::cos(-25.0f * kRadians)});
    }
}

TEST_CASE("the turn as a pose in the controller's space: the runtime's offset") {
    const Pose turn = controllerTilt(30.0f);
    checkVec(turn.position, {0.0f, 0.0f, 0.0f});
    checkVec(transformDirection(turn, {0.0f, 0.0f, -1.0f}),
             {0.0f, std::sin(30.0f * kRadians), -std::cos(30.0f * kRadians)});
    for (const float degrees : {0.0f, std::numeric_limits<float>::quiet_NaN()}) {
        CHECK(controllerTilt(degrees).orientation.w == doctest::Approx(1.0f));
    }
}

TEST_CASE("without a grip pose the aim turns about its own sideways axis") {
    HandState h;
    h.poseValid = true;
    h.aimPose = {Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 90.0f * kRadians), Vec3{0.1f, 1.0f, 0.0f}};
    const Pose before = h.aimPose;
    tiltController(h, 20.0f);
    checkVec(h.aimPose.position, before.position);
    const Pose turn = compose(inverse(before), h.aimPose);
    checkVec(transformDirection(turn, {0.0f, 0.0f, -1.0f}),
             {0.0f, std::sin(20.0f * kRadians), -std::cos(20.0f * kRadians)});
}

TEST_CASE("no angle, a non-finite one, or no valid pose changes nothing") {
    for (const float degrees :
         {0.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        HandState h = someHand();
        const HandState before = h;
        tiltController(h, degrees);
        checkVec(h.gripPose.position, before.gripPose.position);
        CHECK(h.gripPose.orientation.w == doctest::Approx(before.gripPose.orientation.w));
        CHECK(h.aimPose.orientation.x == doctest::Approx(before.aimPose.orientation.x));
    }
    HandState none;
    tiltController(none, 30.0f);
    CHECK(none.gripPose.orientation.w == doctest::Approx(1.0f));
    CHECK(none.aimPose.orientation.w == doctest::Approx(1.0f));
}

TEST_CASE("each hand takes its own angle: the weapon hand's and the other's") {
    for (const Hand weapon : {Hand::Right, Hand::Left}) {
        InputFrame frame;
        const auto script = evr::input::parseTestInput("left.aim = 0, 0\nright.aim = 0, 0\n");
        evr::input::applyTestInput(script, frame);
        evr::input::ControllerSettings settings;
        settings.weaponHandTiltDegrees = -20.0f;
        settings.offHandTiltDegrees = -40.0f;
        fitScriptedHands(frame, script, weapon, settings, {});
        const HandState& w = weapon == Hand::Right ? frame.right : frame.left;
        const HandState& o = weapon == Hand::Right ? frame.left : frame.right;
        CHECK(transformDirection(w.gripPose, {0.0f, 0.0f, -1.0f}).y ==
              doctest::Approx(std::sin(-20.0f * kRadians)));
        CHECK(transformDirection(o.gripPose, {0.0f, 0.0f, -1.0f}).y ==
              doctest::Approx(std::sin(-40.0f * kRadians)));
    }
}
