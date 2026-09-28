#include "xr_math/eye_clip_transform.hpp"

#include "common/pose.hpp"
#include "support/approx.hpp"
#include "xr_math/projection.hpp"

#include <doctest/doctest.h>

#include <array>
#include <ostream>

using evr::Mat4;
using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::test::approxEqual;
using evr::xr_math::Fov;

namespace {

constexpr Fov kCenterFov{-1.0f, 1.0f, 0.9f, -0.95f};
constexpr Fov kLeftEyeFov{-0.95f, 0.75f, 0.85f, -0.9f};

// A head pose somewhere in the world, so the test does not rely on the head sitting at the origin.
Pose headInWorld() {
    return {Quat::fromAxisAngle({0.1f, 1.0f, 0.0f}, 0.6f), {2.0f, 1.7f, -3.0f}};
}

} // namespace

TEST_CASE("eye identical to the centre camera gives the identity transform") {
    // The zero-IPD case: this is the invariant the replay tests check on real frames.
    const Mat4 projection = evr::xr_math::makeProjectionReversedInfinite(kCenterFov, 0.05f).value();
    const Mat4 view = toViewMatrix(headInWorld());

    const auto transform = evr::xr_math::eyeClipTransform(projection, view, projection, view);
    REQUIRE(transform.has_value());
    CHECK(approxEqual(*transform, Mat4::identity()));
}

TEST_CASE("transform maps centre clip space onto eye clip space") {
    const Pose head = headInWorld();
    const Pose leftEyeInHead{Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.05f), {-0.032f, 0.0f, 0.0f}};
    const Pose leftEye = compose(head, leftEyeInHead);

    const Mat4 centerProj = evr::xr_math::makeProjectionReversedInfinite(kCenterFov, 0.05f).value();
    const Mat4 eyeProj = evr::xr_math::makeProjectionReversedInfinite(kLeftEyeFov, 0.05f).value();
    const Mat4 centerView = toViewMatrix(head);
    const Mat4 eyeView = toViewMatrix(leftEye);

    const auto transform = evr::xr_math::eyeClipTransform(centerProj, centerView, eyeProj, eyeView);
    REQUIRE(transform.has_value());

    // World points in front of the head at various depths.
    const std::array<Vec3, 4> pointsInHead{{
        {0.0f, 0.0f, -1.0f},
        {0.5f, -0.2f, -3.0f},
        {-2.0f, 1.0f, -8.0f},
        {0.1f, 0.1f, -0.3f},
    }};
    for (const Vec3& pointInHead : pointsInHead) {
        const evr::Vec4 world = evr::toPoint(transformPoint(head, pointInHead));
        const evr::Vec3 expected = evr::perspectiveDivide(eyeProj * eyeView * world);
        const evr::Vec3 actual = evr::perspectiveDivide(*transform * (centerProj * centerView * world));
        CHECK(approxEqual(actual, expected, 1e-3f));
    }
}

TEST_CASE("singular centre projection is rejected") {
    const Mat4 singular{};
    const Mat4 view = Mat4::identity();
    CHECK_FALSE(evr::xr_math::eyeClipTransform(singular, view, view, view).has_value());
}
