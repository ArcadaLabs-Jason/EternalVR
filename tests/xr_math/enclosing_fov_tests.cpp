#include "xr_math/enclosing_fov.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <initializer_list>
#include <ostream>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::test::approxEqual;
using evr::xr_math::EnclosingShape;
using evr::xr_math::EyeView;
using evr::xr_math::Fov;
using evr::xr_math::FovTangents;

namespace {

constexpr Vec3 kUp{0.0f, 1.0f, 0.0f};
constexpr float kHalfIpd = 0.032f;

// Roughly a current consumer headset: wider on the temporal side, eyes canted outward slightly.
std::array<EyeView, 2> cantedEyes() {
    const float cant = 0.08f;
    return {{
        {{-0.95f, 0.72f, 0.85f, -0.9f}, {Quat::fromAxisAngle(kUp, cant), {-kHalfIpd, 0.0f, 0.0f}}},
        {{-0.72f, 0.95f, 0.85f, -0.9f}, {Quat::fromAxisAngle(kUp, -cant), {kHalfIpd, 0.0f, 0.0f}}},
    }};
}

// True if a head-space point is inside the centre camera's frustum (with a small tolerance for
// points generated exactly on an eye's frustum edge).
bool insideCenterFrustum(const Fov& fov, Vec3 point) {
    const FovTangents t = evr::xr_math::toTangents(fov);
    const float depth = -point.z;
    const float tanX = point.x / depth;
    const float tanY = point.y / depth;
    constexpr float kTolerance = 1e-4f;
    return tanX >= t.left - kTolerance && tanX <= t.right + kTolerance && tanY >= t.down - kTolerance &&
           tanY <= t.up + kTolerance;
}

} // namespace

TEST_CASE("a single centred eye encloses to its own FOV") {
    const Fov eyeFov{-0.9f, 0.8f, 0.7f, -0.75f};
    const std::array<EyeView, 1> eyes{{{eyeFov, Pose::identity()}}};

    const auto fov = evr::xr_math::enclosingFov(eyes, 0.1f);
    REQUIRE(fov.has_value());
    CHECK(approxEqual(fov->angleLeft, eyeFov.angleLeft));
    CHECK(approxEqual(fov->angleRight, eyeFov.angleRight));
    CHECK(approxEqual(fov->angleUp, eyeFov.angleUp));
    CHECK(approxEqual(fov->angleDown, eyeFov.angleDown));
}

TEST_CASE("enclosing FOV contains every point either eye sees beyond the minimum depth") {
    const auto eyes = cantedEyes();
    const float minDepth = 0.2f;
    const auto fov = evr::xr_math::enclosingFov(eyes, minDepth);
    REQUIRE(fov.has_value());

    // Sweep each eye's frustum on a grid of directions (edges included) and a range of distances,
    // keeping only points at or beyond the minimum depth.
    constexpr int kSteps = 8;
    int pointsChecked = 0;
    for (const EyeView& eye : eyes) {
        const FovTangents t = evr::xr_math::toTangents(eye.fov);
        for (int i = 0; i <= kSteps; ++i) {
            for (int j = 0; j <= kSteps; ++j) {
                const float u = static_cast<float>(i) / static_cast<float>(kSteps);
                const float v = static_cast<float>(j) / static_cast<float>(kSteps);
                const float tanX = t.left + (t.right - t.left) * u;
                const float tanY = t.down + (t.up - t.down) * v;
                for (const float distance : {0.15f, 0.2f, 0.5f, 2.0f, 50.0f, 10000.0f}) {
                    const Vec3 inEye{tanX * distance, tanY * distance, -distance};
                    const Vec3 inHead = transformPoint(eye.poseInHead, inEye);
                    if (-inHead.z < minDepth) {
                        continue;
                    }
                    ++pointsChecked;
                    CHECK(insideCenterFrustum(*fov, inHead));
                }
            }
        }
    }
    CHECK(pointsChecked > 500);
}

TEST_CASE("enclosing FOV is wider than either eye alone") {
    const auto eyes = cantedEyes();
    const auto fov = evr::xr_math::enclosingFov(eyes, 0.2f);
    REQUIRE(fov.has_value());
    // Left bound comes from the left eye's temporal side, pushed out by cant and eye offset.
    CHECK(fov->angleLeft < eyes[0].fov.angleLeft);
    CHECK(fov->angleRight > eyes[1].fov.angleRight);
}

TEST_CASE("a larger minimum depth gives a tighter FOV") {
    const auto eyes = cantedEyes();
    const auto nearFov = evr::xr_math::enclosingFov(eyes, 0.1f);
    const auto farFov = evr::xr_math::enclosingFov(eyes, 1.0f);
    REQUIRE(nearFov.has_value());
    REQUIRE(farFov.has_value());
    CHECK(farFov->angleLeft > nearFov->angleLeft);
    CHECK(farFov->angleRight < nearFov->angleRight);
}

TEST_CASE("symmetric shape contains the asymmetric one") {
    const Fov eyeFov{-0.95f, 0.72f, 0.85f, -0.9f};
    const std::array<EyeView, 1> eyes{{{eyeFov, Pose::identity()}}};
    const auto symmetric = evr::xr_math::enclosingFov(eyes, 0.1f, EnclosingShape::Symmetric);
    REQUIRE(symmetric.has_value());
    CHECK(approxEqual(symmetric->angleLeft, -0.95f));
    CHECK(approxEqual(symmetric->angleRight, 0.95f));
    CHECK(approxEqual(symmetric->angleUp, 0.9f));
    CHECK(approxEqual(symmetric->angleDown, -0.9f));
}

TEST_CASE("invalid inputs yield no FOV") {
    const auto eyes = cantedEyes();
    CHECK_FALSE(evr::xr_math::enclosingFov(eyes, 0.0f).has_value());
    CHECK_FALSE(evr::xr_math::enclosingFov({}, 0.1f).has_value());

    // An eye turned sideways has frustum edges pointing backward relative to the centre camera.
    const std::array<EyeView, 1> sideways{
        {{{-0.9f, 0.9f, 0.9f, -0.9f}, {Quat::fromAxisAngle(kUp, 1.6f), {}}}}};
    CHECK_FALSE(evr::xr_math::enclosingFov(sideways, 0.1f).has_value());
}
