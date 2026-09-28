#include "xr_math/head_aim.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>

using evr::Quat;
using evr::Vec3;
using evr::test::approxEqual;
using evr::xr_math::anglesFromAxis;
using evr::xr_math::axisFromAngles;
using evr::xr_math::HeadAimState;
using evr::xr_math::headAimStep;
using evr::xr_math::headAngles;
using evr::xr_math::IdAngles;
using evr::xr_math::isOrthonormal;
using evr::xr_math::normalize180;
using evr::xr_math::noteWritten;
using evr::xr_math::openXrToIdTech;

namespace {

float radians(float degrees) {
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

bool approxAngles(const IdAngles& a, const IdAngles& b, float epsilon = 1e-2f) {
    return approxEqual(normalize180(a.pitch - b.pitch), 0.0f, epsilon) &&
           approxEqual(normalize180(a.yaw - b.yaw), 0.0f, epsilon) &&
           approxEqual(normalize180(a.roll - b.roll), 0.0f, epsilon);
}

} // namespace

TEST_CASE("angles wrap into [-180, 180)") {
    CHECK(approxEqual(normalize180(190.0f), -170.0f));
    CHECK(approxEqual(normalize180(-190.0f), 170.0f));
    CHECK(approxEqual(normalize180(540.0f), -180.0f));
    CHECK(approxEqual(normalize180(45.0f), 45.0f));
}

TEST_CASE("idAngles follow the id Tech convention") {
    // Yaw turns toward +Y (left); positive pitch looks down.
    CHECK(approxEqual(axisFromAngles({0.0f, 90.0f, 0.0f}).forward, Vec3{0.0f, 1.0f, 0.0f}));
    CHECK(approxEqual(axisFromAngles({30.0f, 0.0f, 0.0f}).forward,
                      Vec3{std::cos(radians(30.0f)), 0.0f, -0.5f}));
    CHECK(approxEqual(axisFromAngles({0.0f, 0.0f, 0.0f}).left, Vec3{0.0f, 1.0f, 0.0f}));
    CHECK(isOrthonormal(axisFromAngles({-25.0f, 130.0f, 10.0f})));
}

TEST_CASE("angles survive a round trip through the axis") {
    for (const IdAngles a : {IdAngles{0.0f, 0.0f, 0.0f}, IdAngles{-40.0f, 170.0f, 0.0f},
                             IdAngles{20.0f, -60.0f, 15.0f}, IdAngles{85.0f, 45.0f, -30.0f}}) {
        CHECK(approxAngles(anglesFromAxis(axisFromAngles(a)), a));
    }
}

TEST_CASE("head angles come from the headset orientation") {
    // Head turned 30 degrees left and tilted 20 degrees up in OpenXR terms.
    const Quat yaw = Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, radians(30.0f));
    const Quat pitch = Quat::fromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, radians(20.0f));
    const IdAngles a = headAngles(openXrToIdTech(yaw * pitch));
    CHECK(approxEqual(a.yaw, 30.0f, 1e-2f));
    CHECK(approxEqual(a.pitch, -20.0f, 1e-2f)); // looking up is negative pitch
}

TEST_CASE("head aim moves the game's angles to body plus head") {
    HeadAimState state;
    // Mouse yaw 10, game pitch 5; head 30 left, 20 up.
    auto step = headAimStep(state, {5.0f, 10.0f, 0.0f}, 0.0f, {-20.0f, 30.0f, 0.0f}, false);
    CHECK(approxEqual(step.bodyYaw, 10.0f));
    CHECK(approxEqual(step.deltaYaw, 30.0f));
    CHECK(approxEqual(step.deltaPitch, -25.0f));
    noteWritten(state, 30.0f);
    // Next frame the game shows 40 (applied) plus 5 of mouse; the head moved to 35.
    step = headAimStep(state, {-20.0f, 45.0f, 0.0f}, 30.0f, {-18.0f, 35.0f, 0.0f}, false);
    CHECK(approxEqual(step.bodyYaw, 15.0f));
    CHECK(approxEqual(step.deltaYaw, 5.0f));
    CHECK(approxEqual(step.deltaPitch, 2.0f));
}

TEST_CASE("head aim wraps yaw across 180 degrees") {
    HeadAimState state;
    headAimStep(state, {0.0f, 170.0f, 0.0f}, 0.0f, {0.0f, 175.0f, 0.0f}, false);
    noteWritten(state, 175.0f);
    const auto step = headAimStep(state, {0.0f, -15.0f, 0.0f}, 175.0f, {0.0f, -170.0f, 0.0f}, false);
    // Game yaw -15 = body + 175, so the body is 170; the head moved 15 degrees further left.
    CHECK(approxEqual(step.bodyYaw, 170.0f));
    CHECK(approxEqual(step.deltaYaw, 15.0f));
}

TEST_CASE("a delta the game sets to a value of its own re-aims the view the head had") {
    HeadAimState state;
    headAimStep(state, {0.0f, 0.0f, 0.0f}, 0.0f, {0.0f, 40.0f, 0.0f}, false);
    noteWritten(state, 40.0f);
    // A teleport sets the view to yaw 100: the view stays there, the body takes the head's 40 out of it.
    const auto step = headAimStep(state, {0.0f, 100.0f, 0.0f}, 100.0f, {0.0f, 40.0f, 0.0f}, true);
    CHECK_FALSE(step.restoredWrite);
    CHECK(approxEqual(step.bodyYaw, 60.0f));
    CHECK(approxEqual(step.deltaYaw, 0.0f));
}

TEST_CASE("after a cutscene the game holds the delta at a value head aim wrote") {
    // Measured on the rig: at the end of the e1m1 intro the delta yaw is 118.57 (body 90 + head 28.57),
    // and for about 3 s the game puts it back to 118.57 every frame while the head keeps moving.
    HeadAimState state;
    auto step = headAimStep(state, {0.0f, 90.0f, 0.0f}, 90.0f, {0.0f, 28.57f, 0.0f}, false);
    CHECK(approxEqual(step.bodyYaw, 90.0f));
    noteWritten(state, 118.57f);
    step = headAimStep(state, {0.0f, 118.57f, 0.0f}, 118.57f, {0.0f, 28.62f, 0.0f}, false);
    CHECK(approxEqual(step.bodyYaw, 90.0f));
    noteWritten(state, 118.62f);
    for (float head : {28.68f, 20.0f, -12.0f, -14.06f}) {
        step = headAimStep(state, {0.0f, 118.57f, 0.0f}, 118.57f, {0.0f, head, 0.0f}, true);
        CHECK(step.restoredWrite);
        CHECK(approxEqual(step.bodyYaw, 90.0f));
        CHECK(approxEqual(step.deltaYaw, head - 28.57f, 1e-3f));
        noteWritten(state, 118.57f + step.deltaYaw);
    }
    // The hold ends; the last write (90 - 14.06) stands and the body is still 90.
    step = headAimStep(state, {0.0f, 75.94f, 0.0f}, 75.94f, {0.0f, -14.24f, 0.0f}, false);
    CHECK(approxEqual(step.bodyYaw, 90.0f, 1e-3f));
    // Once the snapshot has left the ring of recent writes, the same value counts as the game's own.
    for (int i = 0; i < 100; ++i) {
        noteWritten(state, 75.94f + static_cast<float>(i) * 0.01f);
    }
    step = headAimStep(state, {0.0f, 118.57f, 0.0f}, 118.57f, {0.0f, 0.0f, 0.0f}, true);
    CHECK_FALSE(step.restoredWrite);
}

TEST_CASE("a delta reset every frame to a value of the game's own does not move the body") {
    HeadAimState state;
    auto step = headAimStep(state, {0.0f, 90.0f, 0.0f}, 90.0f, {0.0f, 30.0f, 0.0f}, false);
    noteWritten(state, 120.0f);
    for (float head : {29.0f, 27.0f, 20.0f}) {
        // The first reset re-aims the view that held the head's 30; the hold keeps that, so the body
        // stays at 60 and the head turns the view from there.
        step = headAimStep(state, {0.0f, 90.0f, 0.0f}, 90.0f, {0.0f, head, 0.0f}, true);
        CHECK_FALSE(step.restoredWrite);
        CHECK(approxEqual(step.bodyYaw, 60.0f));
        CHECK(approxEqual(step.deltaYaw, head - 30.0f));
        noteWritten(state, 90.0f + head - 30.0f);
    }
}

TEST_CASE("head aim clamps the pitch target") {
    HeadAimState state;
    const auto step = headAimStep(state, {0.0f, 0.0f, 0.0f}, 0.0f, {95.0f, 0.0f, 0.0f}, false, 89.0f);
    CHECK(approxEqual(step.deltaPitch, 89.0f));
}

TEST_CASE("test head sway turns the head left and up at a quarter period, and is level at the ends") {
    using evr::xr_math::headSway;
    const IdAngles quarterYaw = headAngles(openXrToIdTech(headSway(20.0f, 0.0f, 4.0f, 1.0)));
    CHECK(approxAngles(quarterYaw, {0.0f, 20.0f, 0.0f}));
    const IdAngles quarterPitch = headAngles(openXrToIdTech(headSway(0.0f, 10.0f, 4.0f, 1.0)));
    CHECK(approxAngles(quarterPitch, {-10.0f, 0.0f, 0.0f})); // id Tech pitch is positive looking down
    const IdAngles threeQuarters = headAngles(openXrToIdTech(headSway(20.0f, 0.0f, 4.0f, 7.0)));
    CHECK(approxAngles(threeQuarters, {0.0f, -20.0f, 0.0f}));
    CHECK(approxAngles(headAngles(openXrToIdTech(headSway(20.0f, 10.0f, 4.0f, 8.0))), {0.0f, 0.0f, 0.0f}));
    const Quat off = headSway(20.0f, 10.0f, 0.0f, 1.0);
    CHECK(approxEqual(off.w, 1.0f));
}

TEST_CASE("the held test head turn is the sway's quarter-period peak") {
    using evr::xr_math::headSway;
    using evr::xr_math::headTurn;
    CHECK(approxAngles(headAngles(openXrToIdTech(headTurn(20.0f, 10.0f))),
                       headAngles(openXrToIdTech(headSway(20.0f, 10.0f, 4.0f, 1.0)))));
    CHECK(approxAngles(headAngles(openXrToIdTech(headTurn(-150.0f, 0.0f))), {0.0f, -150.0f, 0.0f}));
}

TEST_CASE("angles read from game memory must be plausible before head aim writes from them") {
    CHECK(evr::xr_math::plausible(IdAngles{-15.0f, 179.9f, 0.0f}));
    CHECK(evr::xr_math::plausible(IdAngles{720.0f, -540.0f, 3.0f}));
    CHECK_FALSE(evr::xr_math::plausible(IdAngles{std::nanf(""), 0.0f, 0.0f}));
    CHECK_FALSE(evr::xr_math::plausible(IdAngles{0.0f, std::numeric_limits<float>::infinity(), 0.0f}));
    CHECK_FALSE(evr::xr_math::plausible(IdAngles{0.0f, 0.0f, -std::numeric_limits<float>::infinity()}));
    CHECK_FALSE(evr::xr_math::plausible(IdAngles{0.0f, 3.0e7f, 0.0f}));
}
