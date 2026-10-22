#include "xr_math/weapon_pose.hpp"

#include "support/approx.hpp"
#include "xr_math/hand_aim.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <ostream>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::test::approxEqual;
using evr::xr_math::aimErrorDegrees;
using evr::xr_math::anglesOfDirection;
using evr::xr_math::applyLocalOffset;
using evr::xr_math::applyLocalOffsetWithSight;
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
using evr::xr_math::translationKeepingWrist;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

IdViewAxis bodyFacing(float yawDegrees) {
    return axisFromAngles({0.0f, yawDegrees, 0.0f});
}

// The angle between two directions in degrees, in double precision (acos loses it near 0).
double degreesBetween(Vec3 a, Vec3 b) {
    const double ax = a.x, ay = a.y, az = a.z, bx = b.x, by = b.y, bz = b.z;
    const double cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
    return std::atan2(std::sqrt(cx * cx + cy * cy + cz * cz), ax * bx + ay * by + az * bz) * 180.0 /
           std::numbers::pi;
}

// A direction in `axis`'s own frame, in the world.
Vec3 worldOf(const IdViewAxis& axis, Vec3 local) {
    return axis.forward * local.x + axis.left * local.y + axis.up * local.z;
}

// The weapon hand's grip with a rolled aim axis, and the shared table offset.
EyeRelativePose rolledGrip(float roll) {
    EyeRelativePose grip;
    grip.offset = {0.3f, -0.2f, -0.4f};
    grip.axis = axisFromAngles({10.0f, 30.0f, roll});
    return grip;
}
constexpr Vec3 kOffset{-0.25f, 0.18f, 0.22f};
constexpr float kUnitsPerMetre = 39.37f;

// The sights the table names (pitch down, yaw left): the combat shotgun's idle toe-in, the Heavy Cannon's
// iron sights above the bore, the ballista's idle tilt.
constexpr IdAngles kSights[] = {{0.21f, 2.17f, 0.0f}, {-3.45f, 0.19f, 0.0f}, {-0.83f, 1.08f, 0.0f}};

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

TEST_CASE("with its sight row the gun's sight line runs parallel to the aim ray") {
    const EyeRelativePose grip = rolledGrip(47.0f);
    for (const IdAngles& sight : kSights) {
        const Vec3 line = axisFromAngles(sight).forward; // in the model's frame
        const EyeRelativePose placed = applyLocalOffsetWithSight(grip, kOffset, {}, sight, kUnitsPerMetre);
        CHECK(isOrthonormal(placed.axis));
        CHECK(degreesBetween(worldOf(placed.axis, line), grip.axis.forward) < 1e-4);
    }
}

TEST_CASE("without the sight correction the sight line stays off the ray by its own angle") {
    const EyeRelativePose grip = rolledGrip(47.0f);
    for (const IdAngles& sight : kSights) {
        const Vec3 line = axisFromAngles(sight).forward;
        const EyeRelativePose placed = applyLocalOffset(grip, kOffset, {}, kUnitsPerMetre);
        const double expected = degreesBetween(line, {1.0f, 0.0f, 0.0f});
        CHECK(expected > 0.7);
        CHECK(degreesBetween(worldOf(placed.axis, line), grip.axis.forward) ==
              doctest::Approx(expected).epsilon(1e-3));
    }
}

TEST_CASE("the sight correction turns the gun about the grip, which stays on the controller") {
    const EyeRelativePose grip = rolledGrip(47.0f);
    const IdAngles rotations[] = {{}, {2.0f, -3.0f, 5.0f}};
    for (const IdAngles& rotation : rotations) {
        // The model's point at the grip: -L t in the model's frame (game units).
        const IdViewAxis l = axisFromAngles(rotation);
        const Vec3 gripPoint =
            Vec3{dot(l.forward, kOffset), dot(l.left, kOffset), dot(l.up, kOffset)} * -kUnitsPerMetre;
        const EyeRelativePose plain = applyLocalOffset(grip, kOffset, rotation, kUnitsPerMetre);
        CHECK(approxEqual(plain.offset + worldOf(plain.axis, gripPoint), grip.offset, 1e-3f));
        for (const IdAngles& sight : kSights) {
            const EyeRelativePose placed =
                applyLocalOffsetWithSight(grip, kOffset, rotation, sight, kUnitsPerMetre);
            CHECK(approxEqual(placed.offset + worldOf(placed.axis, gripPoint), grip.offset, 1e-3f));
            // The origin itself moves (5 mm or more here): the turn is not about the model's origin.
            CHECK(length(placed.offset - plain.offset) > 0.005f * kUnitsPerMetre);
        }
    }
}

TEST_CASE("the sight correction holds at any controller roll") {
    const IdAngles sight = kSights[1];
    const Vec3 line = axisFromAngles(sight).forward;
    for (const float roll : {-180.0f, -90.0f, 0.0f, 33.0f, 90.0f}) {
        const EyeRelativePose grip = rolledGrip(roll);
        const EyeRelativePose placed = applyLocalOffsetWithSight(grip, kOffset, {}, sight, kUnitsPerMetre);
        CHECK(degreesBetween(worldOf(placed.axis, line), grip.axis.forward) < 1e-4);
    }
}

TEST_CASE("with a row rotation the sight line follows the rotated forward, a deliberate offset") {
    const EyeRelativePose grip = rolledGrip(-20.0f);
    const IdAngles rotation{2.0f, -3.0f, 5.0f};
    const Vec3 wanted = worldOf(grip.axis, axisFromAngles(rotation).forward);
    for (const IdAngles& sight : kSights) {
        const Vec3 line = axisFromAngles(sight).forward;
        const EyeRelativePose placed =
            applyLocalOffsetWithSight(grip, kOffset, rotation, sight, kUnitsPerMetre);
        CHECK(isOrthonormal(placed.axis));
        CHECK(degreesBetween(worldOf(placed.axis, line), wanted) < 1e-4);
    }
}

TEST_CASE("a sight on the model's forward changes nothing") {
    const EyeRelativePose grip = rolledGrip(47.0f);
    const IdAngles rotation{2.0f, -3.0f, 5.0f};
    const EyeRelativePose plain = applyLocalOffset(grip, kOffset, rotation, kUnitsPerMetre);
    const EyeRelativePose placed = applyLocalOffsetWithSight(grip, kOffset, rotation, {}, kUnitsPerMetre);
    CHECK(approxEqual(placed.offset, plain.offset, 1e-4f));
    CHECK(approxEqual(placed.axis.forward, plain.axis.forward));
    CHECK(approxEqual(placed.axis.left, plain.axis.left));
    CHECK(approxEqual(placed.axis.up, plain.axis.up));
}

namespace {

// Where a model point lands for a placement at the eye with the identity axis (metres).
EyeRelativePose
placedAtEye(Vec3 translation, const IdAngles& rotation, const std::optional<IdAngles>& sight) {
    return sight ? applyLocalOffsetWithSight({}, translation, rotation, *sight, 1.0f)
                 : applyLocalOffset({}, translation, rotation, 1.0f);
}

// The model point a placement puts at `at`, in the model's frame.
Vec3 modelPointAt(const EyeRelativePose& placed, Vec3 at) {
    const Vec3 d = at - placed.offset;
    return {dot(placed.axis.forward, d), dot(placed.axis.left, d), dot(placed.axis.up, d)};
}

constexpr Vec3 kRowsWrist{-0.08f, -0.035f, 0.0f};

} // namespace

TEST_CASE("a changed sight turn keeps the model's point at the wrist where the first turn put it") {
    const std::optional<IdAngles> turns[] = {std::nullopt, kSights[0], kSights[1],
                                             IdAngles{-0.15f, 0.15f, 0.0f}};
    const IdAngles rotations[] = {{}, {2.0f, -3.0f, 5.0f}};
    for (const IdAngles& rotation : rotations) {
        for (const std::optional<IdAngles>& from : turns) {
            for (const std::optional<IdAngles>& to : turns) {
                const Vec3 point = modelPointAt(placedAtEye(kOffset, rotation, from), kRowsWrist);
                const Vec3 moved = translationKeepingWrist(kOffset, rotation, from, to, kRowsWrist);
                const EyeRelativePose after = placedAtEye(moved, rotation, to);
                CHECK(approxEqual(after.offset + worldOf(after.axis, point), kRowsWrist, 1e-5f));
                // The new turn itself is kept: the translation does not change the model's axis.
                const EyeRelativePose plain = placedAtEye(kOffset, rotation, to);
                CHECK(approxEqual(after.axis.forward, plain.axis.forward));
                CHECK(approxEqual(after.axis.up, plain.axis.up));
            }
        }
    }
}

TEST_CASE("the same sight turn keeps the row's translation exactly") {
    const IdAngles rotation{2.0f, -3.0f, 5.0f};
    for (const std::optional<IdAngles>& turn :
         {std::optional<IdAngles>{}, std::optional<IdAngles>{kSights[1]}}) {
        const Vec3 moved = translationKeepingWrist(kOffset, rotation, turn, turn, kRowsWrist);
        CHECK(moved.x == kOffset.x);
        CHECK(moved.y == kOffset.y);
        CHECK(moved.z == kOffset.z);
    }
}

TEST_CASE("turned about the grip instead, the wrist would move by the turn's change") {
    // The Heavy Cannon's iron sights to its Precision Bolt's scope: about 3.3 degrees at 8.7 cm from the
    // grip.
    const std::optional<IdAngles> irons = kSights[1];
    const std::optional<IdAngles> scope = IdAngles{-0.15f, 0.15f, 0.0f};
    const Vec3 point = modelPointAt(placedAtEye(kOffset, {}, irons), kRowsWrist);
    const EyeRelativePose aboutGrip = placedAtEye(kOffset, {}, scope);
    const float off = length(aboutGrip.offset + worldOf(aboutGrip.axis, point) - kRowsWrist);
    CHECK(off > 0.004f);
    CHECK(off < 0.006f);
}
