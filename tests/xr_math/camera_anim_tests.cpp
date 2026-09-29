// The hands animation's camera on the head-tracked view (camera_anim.hpp): the chainsaw pickup.

#include "xr_math/camera_anim.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

using evr::Quat;
using evr::Vec3;
using evr::test::approxEqual;
using evr::xr_math::addCameraAnim;
using evr::xr_math::anglesFromAxis;
using evr::xr_math::axisFromAngles;
using evr::xr_math::cameraAnimSize;
using evr::xr_math::cameraAnimWeight;
using evr::xr_math::composeHeadAxis;
using evr::xr_math::IdAngles;
using evr::xr_math::IdViewAxis;
using evr::xr_math::normalize180;
using evr::xr_math::removeCameraAnim;
using evr::xr_math::scaleAngles;

namespace {

bool sameAxis(const IdViewAxis& a, const IdViewAxis& b, float epsilon = 1e-4f) {
    const auto same = [&](Vec3 x, Vec3 y) {
        return approxEqual(x.x, y.x, epsilon) && approxEqual(x.y, y.y, epsilon) &&
               approxEqual(x.z, y.z, epsilon);
    };
    return same(a.forward, b.forward) && same(a.left, b.left) && same(a.up, b.up);
}

// A head turned `yaw` degrees and pitched `pitch` degrees (id Tech angles, pitch down positive).
Quat headAt(float pitch, float yaw) {
    const float y = yaw * std::numbers::pi_v<float> / 360.0f;
    const float p = pitch * std::numbers::pi_v<float> / 360.0f;
    const Quat qy{0.0f, 0.0f, std::sin(y), std::cos(y)}; // about +Z
    const Quat qp{0.0f, std::sin(p), 0.0f, std::cos(p)}; // about +Y (left): positive looks down
    return qy * qp;
}

} // namespace

TEST_CASE("camera animation size and weight: small motion is left out, large plays in full") {
    CHECK(approxEqual(cameraAnimSize({1.0f, -3.0f, 2.0f}), 3.0f));
    CHECK(cameraAnimWeight({0.0f, 0.0f, 0.0f}) == 0.0f);
    // Seen in ordinary play on the rig: under a degree (weapon raise, landing), up to 4 degrees (melee).
    CHECK(cameraAnimWeight({0.7f, -0.2f, -0.75f}) == 0.0f);
    CHECK(cameraAnimWeight({4.0f, -1.0f, 0.5f}) == 0.0f);
    CHECK(cameraAnimWeight({5.0f, 0.0f, 0.0f}) == 0.0f);
    CHECK(cameraAnimWeight({0.0f, 30.0f, 0.0f}) == 1.0f);
    CHECK(cameraAnimWeight({10.0f, 0.0f, 0.0f}) == 1.0f);
    const float mid = cameraAnimWeight({7.5f, 0.0f, 0.0f});
    CHECK(approxEqual(mid, 0.5f));
    // Monotonic, so the view never steps back while an animation grows.
    float last = 0.0f;
    for (float s = 0.0f; s <= 12.0f; s += 0.1f) {
        const float w = cameraAnimWeight({s, 0.0f, 0.0f});
        CHECK(w >= last);
        last = w;
    }
    CHECK(cameraAnimWeight({NAN, 0.0f, 0.0f}) == 0.0f);
    const IdAngles half = scaleAngles({10.0f, -20.0f, 4.0f}, 0.5f);
    CHECK(approxEqual(half.pitch, 5.0f));
    CHECK(approxEqual(half.yaw, -10.0f));
    CHECK(approxEqual(half.roll, 2.0f));
}

TEST_CASE("adding the animation follows the game's rule: angles added, pitch clamped") {
    const IdViewAxis view = axisFromAngles({10.0f, 45.0f, 0.0f});
    const IdAngles a = anglesFromAxis(addCameraAnim(view, {25.0f, -30.0f, 5.0f}));
    CHECK(approxEqual(a.pitch, 35.0f, 1e-3f));
    CHECK(approxEqual(normalize180(a.yaw - 15.0f), 0.0f, 1e-3f));
    CHECK(approxEqual(a.roll, 5.0f, 1e-3f));
    const IdAngles clamped = anglesFromAxis(addCameraAnim(view, {85.0f, 0.0f, 0.0f}));
    CHECK(approxEqual(clamped.pitch, 89.0f, 1e-2f));
    CHECK(sameAxis(addCameraAnim(view, {}), view));
}

TEST_CASE("removing the animation gives the game's own view back") {
    const IdViewAxis base = axisFromAngles({-12.0f, 170.0f, 0.0f});
    const IdAngles added{30.0f, 25.0f, -4.0f};
    const IdViewAxis game = addCameraAnim(base, added);
    const auto removed = removeCameraAnim(game, added);
    REQUIRE(removed.has_value());
    CHECK(sameAxis(*removed, base));
    // Nothing added: the view itself.
    const auto same = removeCameraAnim(base, {});
    REQUIRE(same.has_value());
    CHECK(sameAxis(*same, base));
}

TEST_CASE("a view the pitch clamp took part of the animation from gives a view that adds back to it") {
    // The game clamped 70 + 40 degrees down to 89: the view taken apart is 49 degrees down (not 70), the
    // one that gives the game's view back with the animation.
    const IdViewAxis clamped = addCameraAnim(axisFromAngles({70.0f, 90.0f, 0.0f}), {40.0f, 0.0f, 0.0f});
    const auto removed = removeCameraAnim(clamped, {40.0f, 0.0f, 0.0f});
    REQUIRE(removed.has_value());
    CHECK(approxEqual(anglesFromAxis(*removed).pitch, 49.0f, 1e-2f));
    CHECK(sameAxis(addCameraAnim(*removed, {40.0f, 0.0f, 0.0f}), clamped));
}

TEST_CASE("the head stays tracked under the animation: head turns move the view as without it") {
    // Body facing +Y, the animation looking 40 degrees down and 20 degrees left.
    const IdViewAxis body = axisFromAngles({0.0f, 90.0f, 0.0f});
    const IdAngles added{40.0f, 20.0f, 0.0f};
    for (float headYaw : {-60.0f, 0.0f, 35.0f}) {
        const IdViewAxis view = addCameraAnim(composeHeadAxis(body, headAt(0.0f, headYaw)), added);
        const IdAngles a = anglesFromAxis(view);
        CHECK(approxEqual(normalize180(a.yaw - (90.0f + headYaw + 20.0f)), 0.0f, 1e-2f));
        CHECK(approxEqual(a.pitch, 40.0f, 1e-2f));
    }
    // A head pitched up 30 degrees sees the animation's 40 degrees down as 10 down.
    const IdAngles up = anglesFromAxis(addCameraAnim(composeHeadAxis(body, headAt(-30.0f, 0.0f)), added));
    CHECK(approxEqual(up.pitch, 10.0f, 1e-2f));
}

TEST_CASE("an orientation comes back from its view axis") {
    using evr::xr_math::quatFromViewAxis;
    using evr::xr_math::viewAxisFromQuat;
    for (const IdAngles a : {IdAngles{0.0f, 0.0f, 0.0f}, IdAngles{30.0f, 120.0f, -10.0f},
                             IdAngles{-80.0f, -170.0f, 45.0f}, IdAngles{10.0f, 180.0f, 170.0f}}) {
        const IdViewAxis axis = axisFromAngles(a);
        CHECK(sameAxis(viewAxisFromQuat(quatFromViewAxis(axis)), axis));
    }
}

TEST_CASE("the animated head gives the animated view, and the plain head without an animation") {
    using evr::xr_math::headWithCameraAnim;
    const IdViewAxis body = axisFromAngles({0.0f, -35.0f, 0.0f});
    const Quat head = headAt(15.0f, 25.0f);
    const IdAngles added{30.0f, -40.0f, 8.0f};
    const Quat animated = headWithCameraAnim(body, head, added);
    CHECK(sameAxis(composeHeadAxis(body, animated), addCameraAnim(composeHeadAxis(body, head), added)));
    CHECK(sameAxis(composeHeadAxis(body, headWithCameraAnim(body, head, {})), composeHeadAxis(body, head)));
}

TEST_CASE("orientations convert between OpenXR and id Tech axes both ways") {
    using evr::xr_math::idTechToOpenXr;
    using evr::xr_math::openXrToIdTech;
    const Quat q = evr::normalize(Quat{0.1f, -0.4f, 0.3f, 0.85f});
    const Quat back = idTechToOpenXr(openXrToIdTech(q));
    CHECK(approxEqual(back.x, q.x));
    CHECK(approxEqual(back.y, q.y));
    CHECK(approxEqual(back.z, q.z));
    CHECK(approxEqual(back.w, q.w));
}
