#include "xr_math/weapon_pose.hpp"

#include "support/approx.hpp"
#include "xr_math/hand_aim.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <ostream>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::test::approxEqual;
using evr::xr_math::aimErrorDegrees;
using evr::xr_math::anglesOfDirection;
using evr::xr_math::applyLocalOffset;
using evr::xr_math::atEye;
using evr::xr_math::axisFromAngles;
using evr::xr_math::axisFromDirection;
using evr::xr_math::controllerRelativeToEye;
using evr::xr_math::EyeRelativePose;
using evr::xr_math::headOffsetInWorld;
using evr::xr_math::IdAngles;
using evr::xr_math::IdViewAxis;
using evr::xr_math::isOrthonormal;
using evr::xr_math::shotFromHand;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

IdViewAxis bodyFacing(float yawDegrees) {
    return axisFromAngles({0.0f, yawDegrees, 0.0f});
}

// A tracking pose at `position` pointing `yawRadians` counter-clockwise from -Z and `pitchRadians` up.
Pose aimPose(Vec3 position, float yawRadians, float pitchRadians = 0.0f) {
    const Quat yaw = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yawRadians);
    const Quat pitch = Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, pitchRadians);
    return {yaw * pitch, position};
}

} // namespace

TEST_CASE("a controller ahead-right-below the head sits ahead-right-below the eye in the world") {
    const Pose head{Quat::identity(), {0.0f, 0.0f, 0.0f}};
    const Pose hand = aimPose({0.2f, -0.4f, -0.3f}, 0.0f);
    // Body facing +X: forward 0.3, left -0.2, up -0.4.
    const EyeRelativePose pose = controllerRelativeToEye(bodyFacing(0.0f), {}, head, hand, 1.0f);
    CHECK(approxEqual(pose.offset, Vec3{0.3f, -0.2f, -0.4f}));
    CHECK(approxEqual(pose.axis.forward, Vec3{1.0f, 0.0f, 0.0f}));
    CHECK(isOrthonormal(pose.axis));

    // Body turned 90 degrees left (facing +Y): the same hand is ahead along +Y.
    const EyeRelativePose turned = controllerRelativeToEye(bodyFacing(90.0f), {}, head, hand, 1.0f);
    CHECK(approxEqual(turned.offset, Vec3{0.2f, 0.3f, -0.4f}));
    CHECK(approxEqual(turned.axis.forward, Vec3{0.0f, 1.0f, 0.0f}));
}

TEST_CASE("the offset is measured from the rendered head, so head position and world scale apply") {
    const Pose head{Quat::identity(), {0.1f, 0.0f, 0.0f}};
    const Pose hand = aimPose({0.3f, -0.4f, -0.3f}, 0.0f);
    const auto body = bodyFacing(0.0f);
    const Vec3 headOffset = headOffsetInWorld(body, head.position, 2.0f);
    const EyeRelativePose pose = controllerRelativeToEye(body, headOffset, head, hand, 2.0f);
    // The hand is 0.3 m right of LOCAL's origin, so 0.6 units right of the eye at 2 units per metre.
    CHECK(approxEqual(pose.offset, Vec3{0.6f, -0.6f, -0.8f}));
}

TEST_CASE("the controller's pointing direction becomes the axis's forward, with its pitch") {
    const Pose head;
    const Pose hand = aimPose({}, kPi / 4.0f, kPi / 6.0f); // 45 degrees left, 30 up
    const EyeRelativePose pose = controllerRelativeToEye(bodyFacing(0.0f), {}, head, hand, 1.0f);
    const auto angles = anglesOfDirection(pose.axis.forward);
    REQUIRE(angles.has_value());
    CHECK(angles->yaw == doctest::Approx(45.0f).epsilon(1e-4));
    CHECK(angles->pitch == doctest::Approx(-30.0f).epsilon(1e-4)); // id Tech pitch is positive down
}

TEST_CASE("an axis from a direction has that forward, no roll, and is orthonormal") {
    const auto level = axisFromDirection({2.0f, 0.0f, 0.0f});
    REQUIRE(level.has_value());
    CHECK(approxEqual(level->forward, Vec3{1.0f, 0.0f, 0.0f}));
    CHECK(approxEqual(level->left, Vec3{0.0f, 1.0f, 0.0f}));
    CHECK(approxEqual(level->up, Vec3{0.0f, 0.0f, 1.0f}));

    const auto tilted = axisFromDirection({1.0f, 1.0f, -1.0f});
    REQUIRE(tilted.has_value());
    CHECK(isOrthonormal(*tilted));
    CHECK(tilted->left.z == doctest::Approx(0.0f));

    const auto straightUp = axisFromDirection({0.0f, 0.0f, 3.0f});
    REQUIRE(straightUp.has_value());
    CHECK(isOrthonormal(*straightUp));

    CHECK_FALSE(axisFromDirection({}).has_value());
    CHECK_FALSE(axisFromDirection({std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}).has_value());
}

TEST_CASE("a local offset moves along the weapon's own axes and turns in its own frame") {
    EyeRelativePose pose;
    pose.offset = {1.0f, 2.0f, 3.0f};
    pose.axis = bodyFacing(90.0f); // forward +Y, left -X
    const EyeRelativePose moved = applyLocalOffset(pose, {-0.25f, 0.1f, 0.2f}, {}, 1.0f);
    CHECK(approxEqual(moved.offset, Vec3{1.0f - 0.1f, 2.0f - 0.25f, 3.2f}));
    CHECK(approxEqual(moved.axis.forward, pose.axis.forward));

    const EyeRelativePose yawed = applyLocalOffset(pose, {}, IdAngles{0.0f, 90.0f, 0.0f}, 1.0f);
    CHECK(approxEqual(yawed.axis.forward, Vec3{-1.0f, 0.0f, 0.0f}));
    CHECK(isOrthonormal(yawed.axis));

    const EyeRelativePose scaled = applyLocalOffset(pose, {1.0f, 0.0f, 0.0f}, {}, 3.0f);
    CHECK(approxEqual(scaled.offset, Vec3{1.0f, 5.0f, 3.0f}));
}

TEST_CASE("the pose follows the game's current eye") {
    EyeRelativePose pose;
    pose.offset = {0.3f, -0.2f, -0.4f};
    CHECK(approxEqual(atEye({10.0f, 20.0f, 1.7f}, pose), Vec3{10.3f, 19.8f, 1.3f}));
}

TEST_CASE("a shot leaves the hand along the hand ray") {
    const Vec3 eye{100.0f, 50.0f, 1.7f};
    const Vec3 direction = normalize(Vec3{1.0f, 0.2f, -0.1f});
    const auto shot = shotFromHand(eye, {0.3f, -0.2f, -0.4f}, direction, 1.0f, 0.0f);
    REQUIRE(shot.has_value());
    CHECK(approxEqual(shot->origin, Vec3{100.3f, 49.8f, 1.3f}));
    CHECK(approxEqual(shot->axis.forward, direction));
    CHECK(isOrthonormal(shot->axis));
    // The fire direction and the view angles aimed at it agree exactly (the M5 aim-error metric).
    const auto angles = anglesOfDirection(direction);
    REQUIRE(angles.has_value());
    CHECK(aimErrorDegrees(*angles, *anglesOfDirection(shot->axis.forward)) < 1e-3f);
}

TEST_CASE("the muzzle distance moves the start along the ray") {
    const auto shot = shotFromHand({}, {0.3f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 1.0f, 0.25f);
    REQUIRE(shot.has_value());
    CHECK(approxEqual(shot->origin, Vec3{0.3f, 0.25f, 0.0f}));
}

TEST_CASE("a start beyond arm's reach is pulled back to it") {
    const auto shot = shotFromHand({}, {3.0f, 4.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 1.0f, 0.0f);
    REQUIRE(shot.has_value());
    CHECK(approxEqual(shot->origin, Vec3{0.6f, 0.8f, 0.0f}));
}

TEST_CASE("unusable inputs give no shot") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(shotFromHand({}, {}, {}, 1.0f, 0.0f).has_value());
    CHECK_FALSE(shotFromHand({nan, 0.0f, 0.0f}, {}, {1.0f, 0.0f, 0.0f}, 1.0f, 0.0f).has_value());
    CHECK_FALSE(shotFromHand({}, {}, {1.0f, 0.0f, 0.0f}, -1.0f, 0.0f).has_value());
}
