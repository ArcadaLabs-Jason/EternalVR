#include "features/input/controller_fit.hpp"

#include "common/pose.hpp"
#include "features/input/controller_tilt.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <numbers>
#include <string>

using evr::compose;
using evr::inverse;
using evr::Pose;
using evr::Quat;
using evr::transformDirection;
using evr::Vec3;
using evr::game::Controller;
using evr::input::ControllerFit;
using evr::input::controllerFit;
using evr::input::ControllerSettings;
using evr::input::fitText;
using evr::input::Hand;
using evr::input::handSpaceOffsets;
using evr::input::HandState;
using evr::input::InputFrame;
using evr::input::scriptedHandPose;
using evr::input::tiltController;

namespace {

constexpr float kRadians = std::numbers::pi_v<float> / 180.0f;

void checkVec(Vec3 a, Vec3 b, double epsilon = 1e-4) {
    CHECK(a.x == doctest::Approx(b.x).epsilon(epsilon));
    CHECK(a.y == doctest::Approx(b.y).epsilon(epsilon));
    CHECK(a.z == doctest::Approx(b.z).epsilon(epsilon));
}

Pose pitched(float degrees, Vec3 position) {
    return {Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, degrees * kRadians), position};
}

// SteamVR's Index (valve_controller_knu_1_0, the right hand): the "grip" and "tip" components it gives as the
// OpenXR grip and aim, in the controller's raw frame, without their small yaw and roll.
const Pose kIndexGrip = pitched(15.392f, {0.0f, -0.015f, 0.13f});
const Pose kIndexTip = pitched(-40.0f, {-0.006f, -0.015f, 0.02f});

// A raw (tracking) pose somewhere in the room, turned every way.
const Pose kRaw{Quat::fromAxisAngle(evr::normalize(Vec3{0.4f, 1.0f, -0.3f}), 70.0f * kRadians),
                Vec3{0.3f, 1.2f, -0.5f}};

// The player's SteamVR raw device offset on both controllers (issue #16): 0.06 m along +Y and along -Z.
const Pose kPlayerRawOffset{Quat::identity(), Vec3{0.0f, 0.06f, -0.06f}};

ControllerSettings angles(float weapon, float off) {
    ControllerSettings s;
    s.weaponHandTiltDegrees = weapon;
    s.offHandTiltDegrees = off;
    return s;
}

} // namespace

TEST_CASE("controller fit: only Valve Index controllers have one") {
    for (const Controller family : evr::game::kControllers) {
        CAPTURE(static_cast<int>(family));
        CHECK((controllerFit(family) == ControllerFit{}) == (family != Controller::ValveIndex));
    }
}

TEST_CASE("controller fit: the Index's angles are the player's launcher angles, by role") {
    const ControllerFit fit = controllerFit(Controller::ValveIndex);
    CHECK(fit.weaponHandTiltDegrees == doctest::Approx(-20.0f));
    CHECK(fit.offHandTiltDegrees == doctest::Approx(-40.0f));
}

TEST_CASE("controller fit: the Index's shift moves the grip and the aim as the player's SteamVR offset did") {
    const ControllerFit fit = controllerFit(Controller::ValveIndex);
    // SteamVR moves the raw pose in its own frame; the grip and the aim are fixed to it.
    const Pose rawMoved = compose(kRaw, kPlayerRawOffset);
    const Pose grip = compose(kRaw, kIndexGrip);
    const Pose aim = compose(kRaw, kIndexTip);
    checkVec(compose(grip, Pose{Quat::identity(), fit.gripShift}).position,
             compose(rawMoved, kIndexGrip).position);
    checkVec(compose(aim, Pose{Quat::identity(), fit.aimShift}).position,
             compose(rawMoved, kIndexTip).position);
    // In each pose's own frame: 4.2 cm up and 7.4 cm forward for the grip, 8.5 cm up for the aim.
    checkVec(fit.gripShift, {0.0f, 0.0419f, -0.0738f}, 2e-3);
    checkVec(fit.aimShift, {0.0f, 0.0845f, -0.0074f}, 2e-3);
}

TEST_CASE("controller fit: one move in the room for the gun at the grip and the shots from the aim") {
    const ControllerFit fit = controllerFit(Controller::ValveIndex);
    for (const Hand hand : {Hand::Right, Hand::Left}) {
        for (const float playerAngle : {0.0f, -20.0f, 35.0f}) {
            const ControllerSettings settings = angles(playerAngle, playerAngle);
            const auto offsets = handSpaceOffsets(fit, settings, hand, Hand::Right);
            const Pose grip = compose(kRaw, kIndexGrip);
            const Pose aim = compose(kRaw, kIndexTip);
            const Vec3 gripMove = compose(grip, offsets.grip).position - grip.position;
            const Vec3 aimMove = compose(aim, offsets.aim).position - aim.position;
            checkVec(gripMove, aimMove);
            CHECK(evr::length(gripMove) == doctest::Approx(0.0849f).epsilon(1e-2));
        }
    }
}

TEST_CASE("controller fit: with the launcher at 0 the hand spaces are the player's whole setup") {
    // His setup: the launcher's -20 / -40 (no fit before) and the SteamVR offset under it.
    const ControllerSettings launcher = angles(-20.0f, -40.0f);
    const ControllerSettings zero = angles(0.0f, 0.0f);
    for (const Hand weapon : {Hand::Right, Hand::Left}) {
        for (const Hand hand : {Hand::Left, Hand::Right}) {
            CAPTURE(hand == weapon);
            const Pose rawMoved = compose(kRaw, kPlayerRawOffset);
            HandState his;
            his.gripValid = his.poseValid = true;
            his.gripPose = compose(rawMoved, kIndexGrip);
            his.aimPose = compose(rawMoved, kIndexTip);
            tiltController(his, evr::input::controllerTiltDegrees(launcher, hand, weapon));

            const auto offsets = handSpaceOffsets(controllerFit(Controller::ValveIndex), zero, hand, weapon);
            const Pose grip = compose(compose(kRaw, kIndexGrip), offsets.grip);
            const Pose aim = compose(compose(kRaw, kIndexTip), offsets.aim);
            checkVec(grip.position, his.gripPose.position);
            checkVec(aim.position, his.aimPose.position);
            for (const Vec3 axis : {Vec3{0.0f, 0.0f, -1.0f}, Vec3{0.0f, 1.0f, 0.0f}}) {
                checkVec(transformDirection(grip, axis), transformDirection(his.gripPose, axis));
                checkVec(transformDirection(aim, axis), transformDirection(his.aimPose, axis));
            }
        }
    }
}

TEST_CASE("controller fit: the launcher's angle adds to the fit's, so his old -20 / -40 would count twice") {
    const ControllerFit fit = controllerFit(Controller::ValveIndex);
    const ControllerSettings old = angles(-20.0f, -40.0f);
    CHECK(evr::input::handTiltDegrees(fit, old, Hand::Right, Hand::Right) == doctest::Approx(-40.0f));
    CHECK(evr::input::handTiltDegrees(fit, old, Hand::Left, Hand::Right) == doctest::Approx(-80.0f));
    // The roles follow the weapon hand: left-handed, the left controller takes the weapon hand's -20.
    CHECK(evr::input::handTiltDegrees(fit, angles(0.0f, 0.0f), Hand::Left, Hand::Left) ==
          doctest::Approx(-20.0f));
    CHECK(evr::input::handTiltDegrees(fit, angles(0.0f, 0.0f), Hand::Right, Hand::Left) ==
          doctest::Approx(-40.0f));
    // Without a fit the angle is the launcher's alone, as before.
    CHECK(evr::input::handTiltDegrees(ControllerFit{}, old, Hand::Left, Hand::Right) ==
          doctest::Approx(-40.0f));
    const auto none = handSpaceOffsets(ControllerFit{}, ControllerSettings{}, Hand::Right, Hand::Right);
    checkVec(none.grip.position, {});
    CHECK(none.aim.orientation.w == doctest::Approx(1.0f));
}

TEST_CASE("controller fit: a scripted hand keeps its grip at its aim and moves by the aim's shift") {
    const ControllerFit fit = controllerFit(Controller::ValveIndex);
    const Pose pose{Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 30.0f * kRadians), Vec3{0.2f, 1.0f, -0.4f}};
    const Pose out = scriptedHandPose(pose, fit, ControllerSettings{}, Hand::Right, Hand::Right);
    checkVec(transformDirection(compose(inverse(pose), out), {0.0f, 0.0f, -1.0f}),
             {0.0f, std::sin(-20.0f * kRadians), -std::cos(-20.0f * kRadians)});
    checkVec(compose(inverse(pose), out).position, fit.aimShift);
}

TEST_CASE(
    "controller fit: only the scripted hands take the fit, each its own; a runtime hand is left alone") {
    // The left hand scripted on an Index, the right one as the runtime located it (its spaces already carry
    // its fit and angle: turning it here again was the double turn of a rig run with scripted input).
    const auto script = evr::input::parseTestInput("left.aim = 0, 0\n");
    InputFrame frame;
    frame.right.poseValid = frame.right.gripValid = true;
    frame.right.aimPose = frame.right.gripPose = kRaw;
    evr::input::applyTestInput(script, frame);
    const Pose leftBefore = frame.left.aimPose;
    const std::array<ControllerFit, 2> fits{controllerFit(Controller::ValveIndex), ControllerFit{}};
    evr::input::fitScriptedHands(frame, script, Hand::Right, angles(-10.0f, -5.0f), fits);
    checkVec(frame.right.aimPose.position, kRaw.position);
    CHECK(frame.right.aimPose.orientation.x == doctest::Approx(kRaw.orientation.x));
    CHECK(frame.right.gripPose.orientation.w == doctest::Approx(kRaw.orientation.w));
    // The left (off) hand: the Index's -40 and the launcher's -5, and the aim's shift, the grip with it.
    const Pose turn = compose(inverse(leftBefore), frame.left.aimPose);
    checkVec(transformDirection(turn, {0.0f, 0.0f, -1.0f}),
             {0.0f, std::sin(-45.0f * kRadians), -std::cos(-45.0f * kRadians)});
    checkVec(turn.position, fits[0].aimShift);
    checkVec(frame.left.gripPose.position, frame.left.aimPose.position);
    // The same frame with the fits swapped: the left hand takes no fit, the launcher's angle alone.
    InputFrame swapped;
    evr::input::applyTestInput(script, swapped);
    evr::input::fitScriptedHands(swapped, script, Hand::Right, angles(-10.0f, -5.0f), {fits[1], fits[0]});
    checkVec(compose(inverse(leftBefore), swapped.left.aimPose).position, {});
    checkVec(transformDirection(compose(inverse(leftBefore), swapped.left.aimPose), {0.0f, 0.0f, -1.0f}),
             {0.0f, std::sin(-5.0f * kRadians), -std::cos(-5.0f * kRadians)});
}

TEST_CASE("controller fit: only on SteamVR, whose Index poses the numbers come from") {
    for (const char* name : {"SteamVR/OpenXR", "SteamVR"}) {
        CHECK(evr::input::runtimeHasFits(name));
        CHECK(controllerFit(Controller::ValveIndex, name) == controllerFit(Controller::ValveIndex));
    }
    for (const char* name : {"", "Oculus", "VirtualDesktopXR", "Monado(XRT) by Collabora et al", "WiVRn"}) {
        CAPTURE(name);
        CHECK_FALSE(evr::input::runtimeHasFits(name));
        CHECK(controllerFit(Controller::ValveIndex, name) == ControllerFit{});
    }
    CHECK(controllerFit(Controller::OculusTouch, "SteamVR/OpenXR") == ControllerFit{});
}

TEST_CASE("controller fit: a refused refit is tried again a few times, seconds apart, per family") {
    evr::input::RefitRetry retry;
    CHECK(retry.take(Controller::ValveIndex, 10.0));
    CHECK_FALSE(retry.take(Controller::ValveIndex, 10.5));
    CHECK_FALSE(retry.take(Controller::ValveIndex, 12.9));
    double t = 13.0;
    for (int i = 1; i < evr::input::kRefitAttempts; ++i, t += evr::input::kRefitRetrySeconds) {
        CHECK(retry.take(Controller::ValveIndex, t));
    }
    CHECK(retry.exhausted(Controller::ValveIndex));
    CHECK_FALSE(retry.take(Controller::ValveIndex, t + 100.0));
    // Another family starts over at once.
    CHECK_FALSE(retry.exhausted(Controller::OculusTouch));
    CHECK(retry.take(Controller::OculusTouch, t + 100.1));
    CHECK(retry.take(Controller::ValveIndex, t + 100.2));
}

TEST_CASE("controller fit: the log line names the hand, its family, the fit and its whole angle") {
    const std::string index = fitText(Hand::Right, "valve_index", controllerFit(Controller::ValveIndex), true,
                                      angles(5.0f, 0.0f), Hand::Right);
    CHECK(index == "the right hand's valve_index controller (the weapon hand): its fit moves it 8.5 cm (grip "
                   "frame 0.0 4.2 -7.4 cm, aim frame 0.0 8.5 -0.7 cm) and turns it -20 deg; with the "
                   "launcher's 5 deg, controller angle -15 deg");
    const std::string off = fitText(Hand::Left, "valve_index", controllerFit(Controller::ValveIndex), true,
                                    angles(5.0f, 0.0f), Hand::Right);
    CHECK(off.find("the left hand's valve_index controller (the off hand): its fit") == 0);
    CHECK(off.find("turns it -40 deg; with the launcher's 0 deg, controller angle -40 deg") !=
          std::string::npos);
    CHECK(
        fitText(Hand::Left, "oculus_touch", ControllerFit{}, false, angles(0.0f, -10.0f), Hand::Right) ==
        "the left hand's oculus_touch controller (the off hand): no fit of its own; controller angle -10 deg "
        "(the launcher's)");
    CHECK(fitText(Hand::Right, "valve_index", ControllerFit{}, true, angles(0.0f, 0.0f), Hand::Right) ==
          "the right hand's valve_index controller (the weapon hand): no fit on this runtime (the fit is "
          "worked "
          "out from SteamVR's poses); controller angle 0 deg (the launcher's)");
}
