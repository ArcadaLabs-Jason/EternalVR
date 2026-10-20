#include "xr_math/launcher_pose.hpp"

#include "support/approx.hpp"
#include "xr_math/head_aim.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <ostream>

using evr::Vec3;
using evr::test::approxEqual;
using evr::xr_math::axisFromAngles;
using evr::xr_math::axisFromDirection;
using evr::xr_math::equipmentLaunch;
using evr::xr_math::EyeRelativePose;
using evr::xr_math::IdViewAxis;
using evr::xr_math::isOrthonormal;
using evr::xr_math::launcherAtHead;
using evr::xr_math::Shot;

namespace {

// The angle between two directions in degrees.
double degreesBetween(Vec3 a, Vec3 b) {
    const double ax = a.x, ay = a.y, az = a.z, bx = b.x, by = b.y, bz = b.z;
    const double cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
    return std::atan2(std::sqrt(cx * cx + cy * cy + cz * cz), ax * bx + ay * by + az * bz) * 180.0 /
           std::numbers::pi;
}

Vec3 worldOf(const IdViewAxis& axis, Vec3 local) {
    return axis.forward * local.x + axis.left * local.y + axis.up * local.z;
}

Vec3 localOf(const IdViewAxis& axis, Vec3 world) {
    return {evr::dot(axis.forward, world), evr::dot(axis.left, world), evr::dot(axis.up, world)};
}

// A rig run (simulator, hand aim, Equipment aim = Head, game units are metres): the head looking ahead at yaw
// 105, the weapon hand 60 degrees to its right at yaw 45, held 0.2 m right of, 0.3 m below and 0.4 m ahead of
// the head; the arms model at the hand along its aim ray. The left shoulder launcher's model, from the hook's
// log line (the model's and the arms model's poses, at rest and through a Belch): 0.28 behind the arms
// model's origin, 0.05 to its left and 0.07 below it, along the arms model's forward (the launcher's body
// reaches forward from there).
const IdViewAxis kHeadAxis = axisFromAngles({0.0f, 105.0f, 0.0f});
const IdViewAxis kHandAxis = axisFromAngles({0.0f, 45.0f, 0.0f});
const Vec3 kHeadOffset{0.05f, -0.02f, 0.03f};
const Vec3 kJointInArms{-0.28f, 0.05f, -0.07f};
const IdViewAxis kJointAxisInArms{};

EyeRelativePose armsAtHand() {
    EyeRelativePose arms;
    arms.offset = kHeadOffset + worldOf(kHeadAxis, Vec3{0.4f, -0.2f, -0.3f});
    arms.axis = kHandAxis;
    return arms;
}

// The launcher's world pose (relative to the eye) for the arms model at `arms`, its joint at `jointAxis` in
// the arms model.
EyeRelativePose launcherOn(const EyeRelativePose& arms, const IdViewAxis& jointAxis = kJointAxisInArms) {
    EyeRelativePose launcher;
    launcher.offset = arms.offset + worldOf(arms.axis, kJointInArms);
    launcher.axis.forward = worldOf(arms.axis, jointAxis.forward);
    launcher.axis.left = worldOf(arms.axis, jointAxis.left);
    launcher.axis.up = worldOf(arms.axis, jointAxis.up);
    return launcher;
}

EyeRelativePose head() {
    return {kHeadOffset, kHeadAxis};
}

} // namespace

TEST_CASE("launcherAtHead: the arms at the weapon hand, the launcher goes over the head's left shoulder") {
    const EyeRelativePose arms = armsAtHand();
    const EyeRelativePose game = launcherOn(arms);
    // As the game draws it: 60 degrees off the head's view, ahead of the face.
    CHECK(degreesBetween(game.axis.forward, kHeadAxis.forward) > 55.0);
    const std::optional<EyeRelativePose> moved = launcherAtHead(arms, game, head(), kHeadAxis.forward);
    REQUIRE(moved.has_value());
    // At the head, where the game's own view puts it: the joint's offset in the head's frame.
    const Vec3 fromHead = localOf(kHeadAxis, moved->offset - kHeadOffset);
    CHECK(approxEqual(fromHead, kJointInArms, 1e-4f));
    CHECK(fromHead.y > 0.0f); // left of the head
    CHECK(fromHead.z < 0.0f); // below the eyes
    // Along the head's view.
    CHECK(degreesBetween(moved->axis.forward, kHeadAxis.forward) < 0.01);
    CHECK(isOrthonormal(moved->axis));
}

TEST_CASE("launcherAtHead: the joint's own turn in the arms model is kept") {
    const EyeRelativePose arms = armsAtHand();
    const IdViewAxis turned = axisFromAngles({10.0f, -5.0f, 0.0f}); // 10 degrees down, 5 to the right
    const EyeRelativePose game = launcherOn(arms, turned);
    const std::optional<EyeRelativePose> moved = launcherAtHead(arms, game, head(), kHeadAxis.forward);
    REQUIRE(moved.has_value());
    CHECK(approxEqual(moved->axis.forward, worldOf(kHeadAxis, turned.forward)));
    CHECK(approxEqual(moved->axis.up, worldOf(kHeadAxis, turned.up)));
    CHECK(degreesBetween(moved->axis.forward, game.axis.forward) > 55.0); // off the weapon hand's ray
}

TEST_CASE("launcherAtHead: the arms already at the head with its axis leave the launcher where it is") {
    EyeRelativePose arms;
    arms.offset = kHeadOffset;
    arms.axis = kHeadAxis;
    const EyeRelativePose game = launcherOn(arms);
    const std::optional<EyeRelativePose> moved = launcherAtHead(arms, game, head(), kHeadAxis.forward);
    REQUIRE(moved.has_value());
    CHECK(approxEqual(moved->offset, game.offset));
    CHECK(approxEqual(moved->axis.forward, game.axis.forward));
    CHECK(approxEqual(moved->axis.left, game.axis.left));
    CHECK(approxEqual(moved->axis.up, game.axis.up));
}

TEST_CASE("launcherAtHead: with the off hand it stays at the shoulder and points along the off hand") {
    const EyeRelativePose arms = armsAtHand();
    const EyeRelativePose game = launcherOn(arms);
    const Vec3 offHand = axisFromAngles({-20.0f, 150.0f, 0.0f}).forward; // up and 45 degrees left of the view
    const std::optional<EyeRelativePose> moved = launcherAtHead(arms, game, head(), offHand);
    const std::optional<EyeRelativePose> headed = launcherAtHead(arms, game, head(), kHeadAxis.forward);
    REQUIRE(moved.has_value());
    REQUIRE(headed.has_value());
    CHECK(approxEqual(moved->offset, headed->offset));
    const IdViewAxis along = *axisFromDirection(offHand);
    CHECK(approxEqual(moved->axis.forward, along.forward));
    CHECK(approxEqual(moved->axis.up, along.up));
    CHECK(degreesBetween(moved->axis.forward, offHand) < 0.01);
}

TEST_CASE("launcherAtHead: the head's roll is left out, as the game's own view has none") {
    const EyeRelativePose arms = armsAtHand();
    const EyeRelativePose game = launcherOn(arms);
    EyeRelativePose rolled = head();
    rolled.axis = axisFromAngles({0.0f, 105.0f, 25.0f});
    const std::optional<EyeRelativePose> level = launcherAtHead(arms, game, head(), kHeadAxis.forward);
    const std::optional<EyeRelativePose> tilted = launcherAtHead(arms, game, rolled, rolled.axis.forward);
    REQUIRE(level.has_value());
    REQUIRE(tilted.has_value());
    CHECK(approxEqual(tilted->offset, level->offset));
    CHECK(approxEqual(tilted->axis.forward, level->axis.forward));
    CHECK(approxEqual(tilted->axis.up, level->axis.up));
}

TEST_CASE("launcherAtHead: the head's pitch carries the launcher with the view, as on a flat screen") {
    const EyeRelativePose arms = armsAtHand();
    const EyeRelativePose game = launcherOn(arms);
    EyeRelativePose down = head();
    down.axis = axisFromAngles({40.0f, 105.0f, 0.0f});
    const std::optional<EyeRelativePose> moved = launcherAtHead(arms, game, down, down.axis.forward);
    REQUIRE(moved.has_value());
    CHECK(approxEqual(localOf(down.axis, moved->offset - kHeadOffset), kJointInArms));
    CHECK(degreesBetween(moved->axis.forward, down.axis.forward) < 0.01);
}

TEST_CASE("launcherAtHead: unusable input moves nothing") {
    const EyeRelativePose arms = armsAtHand();
    const EyeRelativePose game = launcherOn(arms);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(launcherAtHead(arms, game, head(), Vec3{0.0f, 0.0f, 0.0f}).has_value());
    CHECK_FALSE(launcherAtHead(arms, game, head(), Vec3{nan, 0.0f, 0.0f}).has_value());
    EyeRelativePose bad = game;
    bad.offset.x = nan;
    CHECK_FALSE(launcherAtHead(arms, bad, head(), kHeadAxis.forward).has_value());
    EyeRelativePose skewed = arms;
    skewed.axis.left = skewed.axis.forward;
    CHECK_FALSE(launcherAtHead(skewed, game, head(), kHeadAxis.forward).has_value());
}

TEST_CASE("launcherAtHead: the weapon in the left hand (the arms turned and moved for the left glove) "
          "changes nothing") {
    // As the viewmodel hook places the arms for the left glove: the row's turn reflected (left_hand.hpp
    // leftHandTurn: yaw and roll change sign) and the model moved, not turned, until the left glove's wrist
    // sits on the left hand (leftGloveShift), with the weapon hand on the left of the head.
    EyeRelativePose lefty;
    lefty.offset = kHeadOffset + worldOf(kHeadAxis, Vec3{0.4f, 0.2f, -0.3f}) + Vec3{0.04f, -0.09f, 0.02f};
    lefty.axis = axisFromAngles({0.0f, 45.0f - 8.0f, -12.0f});
    const IdViewAxis turned = axisFromAngles({10.0f, -5.0f, 0.0f});
    const std::optional<EyeRelativePose> moved =
        launcherAtHead(lefty, launcherOn(lefty, turned), head(), kHeadAxis.forward);
    const std::optional<EyeRelativePose> righty =
        launcherAtHead(armsAtHand(), launcherOn(armsAtHand(), turned), head(), kHeadAxis.forward);
    REQUIRE(moved.has_value());
    REQUIRE(righty.has_value());
    // Over the head's left shoulder and along the head, as with the weapon in the right hand.
    CHECK(approxEqual(localOf(kHeadAxis, moved->offset - kHeadOffset), kJointInArms));
    CHECK(approxEqual(moved->offset, righty->offset));
    CHECK(approxEqual(moved->axis.forward, righty->axis.forward));
    CHECK(approxEqual(moved->axis.left, righty->axis.left));
    CHECK(approxEqual(moved->axis.up, righty->axis.up));
}

namespace {

// A frag launched in the action-aim rig scenario (hand aim, Equipment aim = Head, the weapon hand 90 degrees
// to the right of the head at yaw 135): the eye, the game's launch origin and direction from the launcher
// drawn at the head along the head's ray (the decl's arc: 2 degrees up), from its log line.
const Vec3 kLaunchEye{-35.09f, -250.42f, 33.26f};
const Vec3 kLaunchOrigin{-35.97f, -250.30f, 33.68f};
const Vec3 kLaunchDirection{-0.707f, 0.707f, 0.035f};
const IdViewAxis kLaunchHead = axisFromAngles({0.0f, 135.0f, 0.0f});
const Vec3 kLaunchWeaponHand{-0.707f, -0.707f, 0.0f};
const EyeRelativePose kLaunchRay{{0.05f, 0.02f, 0.25f}, kLaunchHead};

} // namespace

TEST_CASE("equipmentLaunch: from the launcher drawn at the head, the game's origin and its arc stay") {
    const std::optional<Shot> shot =
        equipmentLaunch(kLaunchEye, kLaunchOrigin, kLaunchDirection, kLaunchHead.forward, kLaunchWeaponHand,
                        kLaunchRay, 1.0f);
    REQUIRE(shot.has_value());
    CHECK(approxEqual(shot->origin, kLaunchOrigin));
    CHECK(degreesBetween(shot->axis.forward, kLaunchDirection) < 0.05);
}

TEST_CASE(
    "equipmentLaunch: a launcher left at the weapon hand launches from the ray's start, turned onto it") {
    // The same launch with the launcher at the weapon hand: the game's direction is the weapon hand's ray
    // with the same arc.
    const IdViewAxis hand = axisFromAngles({0.0f, -135.0f, 0.0f});
    const Vec3 gameDirection = worldOf(hand, Vec3{1.0f, 0.0f, 0.035f});
    const Vec3 gameOrigin = kLaunchEye + worldOf(hand, Vec3{0.3f, -0.1f, -0.2f});
    const std::optional<Shot> shot =
        equipmentLaunch(kLaunchEye, gameOrigin, gameDirection, std::nullopt, hand.forward, kLaunchRay, 1.0f);
    REQUIRE(shot.has_value());
    CHECK(approxEqual(shot->origin, kLaunchEye + kLaunchRay.offset));
    CHECK(degreesBetween(shot->axis.forward, kLaunchDirection) < 0.05);
    CHECK(degreesBetween(gameDirection, kLaunchDirection) > 85.0);
}

TEST_CASE("equipmentLaunch: the ray's start is kept within reach, and unusable directions launch nothing") {
    EyeRelativePose far = kLaunchRay;
    far.offset = Vec3{3.0f, 0.0f, 4.0f};
    const std::optional<Shot> shot = equipmentLaunch(kLaunchEye, kLaunchOrigin, kLaunchDirection,
                                                     std::nullopt, kLaunchWeaponHand, far, 1.0f);
    REQUIRE(shot.has_value());
    CHECK(approxEqual(shot->origin, kLaunchEye + Vec3{0.6f, 0.0f, 0.8f}));
    CHECK_FALSE(equipmentLaunch(kLaunchEye, kLaunchOrigin, Vec3{}, kLaunchHead.forward, kLaunchWeaponHand,
                                kLaunchRay, 1.0f)
                    .has_value());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(equipmentLaunch(kLaunchEye, Vec3{nan, 0.0f, 0.0f}, kLaunchDirection, kLaunchHead.forward,
                                kLaunchWeaponHand, kLaunchRay, 1.0f)
                    .has_value());
}
