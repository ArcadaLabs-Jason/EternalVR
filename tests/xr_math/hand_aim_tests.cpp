#include "xr_math/hand_aim.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <ostream>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::test::approxEqual;
using evr::xr_math::AimCorrection;
using evr::xr_math::aimErrorDegrees;
using evr::xr_math::anglesOfDirection;
using evr::xr_math::axisFromAngles;
using evr::xr_math::closedLoopAim;
using evr::xr_math::convergenceAngles;
using evr::xr_math::handAimAngles;
using evr::xr_math::handRayInWorld;
using evr::xr_math::IdAngles;
using evr::xr_math::IdViewAxis;
using evr::xr_math::pointAlong;
using evr::xr_math::trackingToWorld;
using evr::xr_math::WorldRay;

namespace {

constexpr float kRadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

// OpenXR rotations: yaw about +Y (positive turns left), pitch about +X (positive looks up), roll about
// the pointing axis (-Z).
Quat xrYaw(float degrees) {
    return Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, degrees * kRadiansPerDegree);
}
Quat xrPitch(float degrees) {
    return Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, degrees * kRadiansPerDegree);
}
Quat xrRoll(float degrees) {
    return Quat::fromAxisAngle({0.0f, 0.0f, -1.0f}, degrees * kRadiansPerDegree);
}

IdViewAxis bodyYaw(float degrees) {
    return axisFromAngles({0.0f, degrees, 0.0f});
}

bool sameAngles(const IdAngles& a, const IdAngles& b, float epsilon = 1e-3f) {
    return approxEqual(a.pitch, b.pitch, epsilon) && approxEqual(a.yaw, b.yaw, epsilon) &&
           approxEqual(a.roll, b.roll, epsilon);
}

} // namespace

TEST_CASE("tracking vectors map to id Tech axes as id = (-z, -x, y)") {
    const IdViewAxis body;
    CHECK(approxEqual(trackingToWorld(body, {0.0f, 0.0f, -1.0f}), Vec3{1.0f, 0.0f, 0.0f}));       // forward
    CHECK(approxEqual(trackingToWorld(body, {-1.0f, 0.0f, 0.0f}), Vec3{0.0f, 1.0f, 0.0f}));       // left
    CHECK(approxEqual(trackingToWorld(body, {0.0f, 1.0f, 0.0f}), Vec3{0.0f, 0.0f, 1.0f}));        // up
    CHECK(approxEqual(trackingToWorld(body, {0.0f, 0.0f, -2.0f}, 3.0f), Vec3{6.0f, 0.0f, 0.0f})); // scaled
}

TEST_CASE("a hand pointing straight ahead aims along the body's heading") {
    CHECK(sameAngles(handAimAngles(Quat::identity()), {0.0f, 0.0f, 0.0f}));
    const WorldRay ray = handRayInWorld(bodyYaw(90.0f), {}, Pose::identity(), Pose::identity());
    CHECK(approxEqual(ray.direction, Vec3{0.0f, 1.0f, 0.0f}));
    CHECK(sameAngles(anglesOfDirection(ray.direction).value(), {0.0f, 90.0f, 0.0f}));
}

TEST_CASE("hand yaw and pitch become id Tech yaw and pitch") {
    CHECK(sameAngles(handAimAngles(xrYaw(90.0f)), {0.0f, 90.0f, 0.0f}));    // left is positive yaw
    CHECK(sameAngles(handAimAngles(xrYaw(-30.0f)), {0.0f, -30.0f, 0.0f}));  // right is negative
    CHECK(sameAngles(handAimAngles(xrPitch(30.0f)), {-30.0f, 0.0f, 0.0f})); // up is negative pitch
    CHECK(sameAngles(handAimAngles(xrYaw(45.0f) * xrPitch(-20.0f)), {20.0f, 45.0f, 0.0f}));
}

TEST_CASE("rolling the hand about its pointing ray does not move the aim") {
    const Quat aim = xrYaw(-60.0f) * xrPitch(15.0f);
    for (const float roll : {-90.0f, 35.0f, 170.0f}) {
        CAPTURE(roll);
        CHECK(sameAngles(handAimAngles(aim * xrRoll(roll)), handAimAngles(aim)));
    }
}

TEST_CASE("the hand ray's angles point along its world direction") {
    const Quat aim = xrYaw(20.0f) * xrPitch(-40.0f);
    const WorldRay ray = handRayInWorld(bodyYaw(-75.0f), {}, Pose::identity(), {aim, {}});
    const IdAngles angles = anglesOfDirection(ray.direction).value();
    CHECK(approxEqual(axisFromAngles(angles).forward, ray.direction));
    // Body yaw adds to the hand's own yaw.
    CHECK(angles.yaw == doctest::Approx(-55.0f).epsilon(1e-4));
    CHECK(angles.pitch == doctest::Approx(40.0f).epsilon(1e-4));
}

TEST_CASE("the ray starts at the hand, placed relative to the rendered head") {
    // Head at 1.7 m, hand 0.3 m right, 0.4 m lower and 0.5 m ahead; body facing +Y in the world.
    const Pose head{Quat::identity(), {0.0f, 1.7f, 0.0f}};
    const Pose aim{Quat::identity(), {0.3f, 1.3f, -0.5f}};
    const Vec3 headWorld{100.0f, 200.0f, 50.0f};
    const WorldRay ray = handRayInWorld(bodyYaw(90.0f), headWorld, head, aim, 2.0f);
    // Body-local offset (0.5 ahead, 0.3 right, 0.4 down) x2 units; ahead is +Y and right is +X here.
    CHECK(approxEqual(ray.origin, Vec3{100.6f, 201.0f, 49.2f}));
    CHECK(approxEqual(pointAlong(ray, 10.0f), Vec3{100.6f, 211.0f, 49.2f}));
}

TEST_CASE("closed-loop aim sends the difference to the hand ray, the short way round") {
    const AimCorrection c = closedLoopAim({10.0f, 179.0f, 0.0f}, {-5.0f, -179.0f, 0.0f}, false);
    CHECK_FALSE(c.yielded);
    CHECK(c.deltaYaw == doctest::Approx(2.0f));
    CHECK(c.deltaPitch == doctest::Approx(-15.0f));
    // Applying the correction lands on the target.
    CHECK(aimErrorDegrees({10.0f + c.deltaPitch, 179.0f + c.deltaYaw, 0.0f}, {-5.0f, -179.0f, 0.0f}) < 1e-3f);
}

TEST_CASE("closed-loop aim clamps the pitch target") {
    const AimCorrection c = closedLoopAim({0.0f, 0.0f, 0.0f}, {95.0f, 0.0f, 0.0f}, false, 80.0f);
    CHECK(c.deltaPitch == doctest::Approx(80.0f));
}

TEST_CASE("closed-loop aim yields while the game forces the angles, and on garbage") {
    const AimCorrection forced = closedLoopAim({0.0f, 0.0f, 0.0f}, {10.0f, 30.0f, 0.0f}, true);
    CHECK(forced.yielded);
    CHECK(forced.deltaYaw == 0.0f);
    CHECK(forced.deltaPitch == 0.0f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(closedLoopAim({nan, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, false).yielded);
    CHECK(closedLoopAim({0.0f, 0.0f, 0.0f}, {0.0f, 1e9f, 0.0f}, false).yielded);
}

TEST_CASE("aim error is the angle between the two forward directions") {
    CHECK(aimErrorDegrees({0.0f, 0.0f, 0.0f}, {0.0f, 10.0f, 0.0f}) == doctest::Approx(10.0f));
    CHECK(aimErrorDegrees({0.0f, 179.5f, 0.0f}, {0.0f, -179.5f, 0.0f}) == doctest::Approx(1.0f));
    CHECK(aimErrorDegrees({0.0f, 0.0f, 0.0f}, {0.3f, 0.0f, 0.0f}) == doctest::Approx(0.3f).epsilon(1e-3));
    // Roll does not count, and at the pole yaw means nothing.
    CHECK(aimErrorDegrees({0.0f, 0.0f, 40.0f}, {0.0f, 0.0f, 0.0f}) == doctest::Approx(0.0f));
    CHECK(aimErrorDegrees({90.0f, 0.0f, 0.0f}, {90.0f, 120.0f, 0.0f}) < 1e-3f);
}

TEST_CASE("convergence aim points the eye at the point the hand ray hits") {
    const Vec3 eye{0.0f, 0.0f, 1.7f};
    CHECK(sameAngles(convergenceAngles(eye, {10.0f, 10.0f, 1.7f}).value(), {0.0f, 45.0f, 0.0f}));
    CHECK(sameAngles(convergenceAngles(eye, {10.0f, 0.0f, -8.3f}).value(), {45.0f, 0.0f, 0.0f}));
    // The eye sits above the hand, so converging on a far hit tilts the view slightly down.
    const WorldRay ray{{0.0f, -0.3f, 1.3f}, {1.0f, 0.0f, 0.0f}};
    const IdAngles far = convergenceAngles(eye, pointAlong(ray, 20.0f)).value();
    CHECK(far.pitch > 0.0f);
    CHECK(far.yaw < 0.0f);
    CHECK_FALSE(convergenceAngles(eye, eye).has_value());
    CHECK_FALSE(convergenceAngles(eye, {std::numeric_limits<float>::infinity(), 0.0f, 0.0f}).has_value());
}
