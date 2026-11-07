#include "xr_math/view_attached.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <numbers>

using evr::Quat;
using evr::Vec3;
using evr::xr_math::AttachedPose;
using evr::xr_math::IdViewAxis;
using evr::xr_math::reseatOnView;
using evr::xr_math::viewAxisFromQuat;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

IdViewAxis yawed(float degrees) {
    return viewAxisFromQuat(Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, degrees * kPi / 180.0f));
}

IdViewAxis pitched(float degrees) {
    // Positive pitch looks up: forward (+X) toward up (+Z) is a negative turn about +Y.
    return viewAxisFromQuat(Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, -degrees * kPi / 180.0f));
}

bool approxEqual(const IdViewAxis& a, const IdViewAxis& b) {
    return evr::test::approxEqual(a.forward, b.forward) && evr::test::approxEqual(a.left, b.left) &&
           evr::test::approxEqual(a.up, b.up);
}

} // namespace

TEST_CASE("a view equal to the camera leaves the model where the game put it") {
    const IdViewAxis camera = yawed(30.0f);
    const AttachedPose model{{10.2f, 5.1f, 2.0f}, yawed(35.0f)};
    const AttachedPose placed = reseatOnView(model, {10.0f, 5.0f, 1.8f}, camera, {}, camera);
    CHECK(evr::test::approxEqual(placed.origin, model.origin));
    CHECK(approxEqual(placed.axis, model.axis));
}

TEST_CASE("the head offset moves the model with the eye") {
    const IdViewAxis camera = yawed(0.0f);
    const AttachedPose model{{1.0f, 0.0f, 1.5f}, camera};
    const AttachedPose placed =
        reseatOnView(model, {1.0f, 0.0f, 1.5f}, camera, {0.3f, -0.2f, -0.25f}, camera);
    CHECK(evr::test::approxEqual(placed.origin, Vec3{1.3f, -0.2f, 1.25f}));
    CHECK(approxEqual(placed.axis, camera));
}

TEST_CASE("a model at the camera takes the view's axis") {
    // The cannons: the game puts them at the camera with the camera's axis, the weapon hand's aim under hand
    // aim.
    const IdViewAxis camera = yawed(40.0f);
    const IdViewAxis view = pitched(-20.0f);
    const AttachedPose placed =
        reseatOnView({{0.0f, 0.0f, 2.0f}, camera}, {0.0f, 0.0f, 2.0f}, camera, {}, view);
    CHECK(evr::test::approxEqual(placed.origin, Vec3{0.0f, 0.0f, 2.0f}));
    CHECK(approxEqual(placed.axis, view));
}

TEST_CASE("a model off the camera turns about the camera with the view") {
    // 0.1 below and 0.2 ahead of the camera, which looks along +X. The view looks along +Y.
    const IdViewAxis camera = yawed(0.0f);
    const IdViewAxis view = yawed(90.0f);
    const AttachedPose model{{0.2f, 0.0f, -0.1f}, pitched(10.0f)};
    const AttachedPose placed = reseatOnView(model, {}, camera, {0.0f, 0.0f, 0.05f}, view);
    CHECK(evr::test::approxEqual(placed.origin, Vec3{0.0f, 0.2f, -0.05f}));
    // Its own pitch relative to the camera is kept.
    const IdViewAxis expected =
        viewAxisFromQuat(Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, kPi / 2.0f) *
                         Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, -10.0f * kPi / 180.0f));
    CHECK(approxEqual(placed.axis, expected));
}

TEST_CASE("the model keeps its place in the eyes' frame whatever the camera does") {
    // The flat screen's property: the eyes never see the model from another side. Under hand aim the camera
    // follows the weapon hand while the eyes follow the head; the model's pose relative to the view must be
    // the pose the game gave it relative to the camera.
    const IdViewAxis view = yawed(-15.0f);
    const Vec3 local{0.05f, -0.3f, -0.2f}; // in the camera's frame: ahead, to the right, below
    for (const float aimYaw : {0.0f, 45.0f, -70.0f}) {
        for (const float aimPitch : {-45.0f, 0.0f, 45.0f}) {
            const IdViewAxis camera =
                viewAxisFromQuat(Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, aimYaw * kPi / 180.0f) *
                                 Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, -aimPitch * kPi / 180.0f));
            const Vec3 cameraOrigin{3.0f, -2.0f, 1.7f};
            const AttachedPose model{cameraOrigin + camera.forward * local.x + camera.left * local.y +
                                         camera.up * local.z,
                                     camera};
            const AttachedPose placed = reseatOnView(model, cameraOrigin, camera, {}, view);
            const Vec3 d = placed.origin - cameraOrigin;
            CHECK(evr::test::approxEqual(Vec3{dot(d, view.forward), dot(d, view.left), dot(d, view.up)},
                                         local));
            CHECK(approxEqual(placed.axis, view));
        }
    }
}

TEST_CASE("a vector and an axis taken into a frame and back are unchanged") {
    using evr::xr_math::axisFromFrame;
    using evr::xr_math::axisToFrame;
    using evr::xr_math::fromFrame;
    using evr::xr_math::toFrame;
    const IdViewAxis frame = viewAxisFromQuat(Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 0.7f) *
                                              Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, -0.3f));
    const Vec3 v{0.4f, -1.2f, 2.5f};
    CHECK(evr::test::approxEqual(fromFrame(toFrame(v, frame), frame), v));
    // The frame's own forward is (1, 0, 0) in it.
    CHECK(evr::test::approxEqual(toFrame(frame.forward, frame), Vec3{1.0f, 0.0f, 0.0f}));
    const IdViewAxis axis = pitched(25.0f);
    CHECK(approxEqual(axisFromFrame(axisToFrame(axis, frame), frame), axis));
    // A view kept in the body frame of one yaw and put on another turns with the body: what the published
    // view goes through when the next tick's body yaw has moved.
    const IdViewAxis view = yawed(50.0f);
    CHECK(approxEqual(axisFromFrame(axisToFrame(view, yawed(30.0f)), yawed(40.0f)), yawed(60.0f)));
}
