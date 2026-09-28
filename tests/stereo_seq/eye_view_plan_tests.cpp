#include "stereo_seq/eye_view_plan.hpp"
#include "xr_math/stereo_view.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::stereo_seq::Eye;
using evr::stereo_seq::planEyeView;
using evr::stereo_seq::SeqViewSettings;
using evr::test::approxEqual;
using evr::xr_math::EngineMatrix;
using evr::xr_math::engineProjection;
using evr::xr_math::eyeInHeadFromSpace;
using evr::xr_math::eyeViewInWorld;
using evr::xr_math::Fov;
using evr::xr_math::fovOfEngineProjection;
using evr::xr_math::IdViewAxis;

namespace {

float radians(float degrees) {
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

// The engine's matrix for a symmetric 100 x 100 degree view (the depth rows every eye reuses).
EngineMatrix gameMatrix() {
    const float t = std::tan(radians(50.0f));
    EngineMatrix m{};
    m[0] = 1.0f / t;
    m[5] = 1.0f / t;
    m[10] = -1.0001f;
    m[11] = -2.0001f;
    m[14] = -1.0f;
    return m;
}

Fov leftEye() {
    return {radians(-54.0f), radians(40.0f), radians(44.0f), radians(-55.0f)};
}
Fov rightEye() {
    return {radians(-40.0f), radians(54.0f), radians(44.0f), radians(-55.0f)};
}

} // namespace

TEST_CASE("eye view plan: per-eye pose and flags") {
    const SeqViewSettings s;
    const auto l = planEyeView(Eye::Left, s);
    const auto r = planEyeView(Eye::Right, s);
    CHECK(l.writePose);
    CHECK(r.writePose);
    CHECK(l.forceFullResolution);
    CHECK(r.forceFullResolution);
    CHECK_FALSE(l.skipAutoExposureUpdate); // exposure adapts in eye L's frame
    CHECK(r.skipAutoExposureUpdate);
    CHECK_FALSE(l.discontinuousViewPosition);
    CHECK(l.inhibitModelFovScale);
    CHECK(r.inhibitModelFovScale);
}

TEST_CASE("eye view plan: same view (S1) keeps the game's view but applies the per-view flags") {
    SeqViewSettings s;
    s.sameView = true;
    const auto r = planEyeView(Eye::Right, s);
    CHECK_FALSE(r.writePose);
    CHECK_FALSE(r.inhibitModelFovScale);
    CHECK(r.forceFullResolution);
    CHECK(r.skipAutoExposureUpdate);
}

TEST_CASE("eye view plan: switches") {
    SeqViewSettings s;
    s.fullResolution = false;
    s.exposureOnce = false;
    s.discontinuous = true;
    s.inhibitModelFov = false;
    const auto r = planEyeView(Eye::Right, s);
    CHECK_FALSE(r.forceFullResolution);
    CHECK_FALSE(r.skipAutoExposureUpdate);
    CHECK(r.discontinuousViewPosition);
    CHECK_FALSE(r.inhibitModelFovScale);
    const auto mono = planEyeView(Eye::Mono, s);
    CHECK_FALSE(mono.writePose);
    CHECK_FALSE(mono.discontinuousViewPosition);
}

TEST_CASE("per-eye matrices: one tick's two eyes from LOCAL-space locates") {
    // Head 1.6 m up, turned 30 degrees left; the eyes 64 mm apart, located in LOCAL like the camera hook.
    const Quat headQ{0.0f, std::sin(radians(15.0f)), 0.0f, std::cos(radians(15.0f))}; // 30 degrees about +Y
    const Pose head{headQ, {0.1f, 1.6f, -0.2f}};
    const Pose leftInSpace = evr::compose(head, Pose{Quat{}, {-0.032f, 0.0f, 0.0f}});
    const Pose rightInSpace = evr::compose(head, Pose{Quat{}, {0.032f, 0.0f, 0.0f}});
    const Pose leftInHead = eyeInHeadFromSpace(head, leftInSpace);
    const Pose rightInHead = eyeInHeadFromSpace(head, rightInSpace);
    CHECK(approxEqual(leftInHead.position, Vec3{-0.032f, 0.0f, 0.0f}));
    CHECK(approxEqual(rightInHead.position, Vec3{0.032f, 0.0f, 0.0f}));

    // Body facing +X in id Tech (forward x, left y, up z), at 40 units per metre.
    const IdViewAxis body{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    const float scale = 40.0f;
    const auto l = eyeViewInWorld(body, headQ, leftInHead, scale);
    const auto r = eyeViewInWorld(body, headQ, rightInHead, scale);
    // The eyes sit symmetrically about the head centre, 64 mm apart at the world scale, level.
    CHECK(approxEqual(l.offset + r.offset, Vec3{0.0f, 0.0f, 0.0f}, 1e-3f));
    CHECK(approxEqual(length(l.offset - r.offset), 0.064f * scale, 1e-3f));
    CHECK(approxEqual(l.offset.z, 0.0f, 1e-4f));
    // Both eyes look the same way (parallel eyes), and eye L is on the view's left.
    CHECK(approxEqual(l.axis.forward, r.axis.forward));
    CHECK(evr::dot(l.offset - r.offset, l.axis.left) > 0.0f);

    // Each eye's asymmetric projection keeps the game's depth rows and round-trips its own FOV.
    const EngineMatrix game = gameMatrix();
    const auto pl = engineProjection(leftEye(), game);
    const auto pr = engineProjection(rightEye(), game);
    REQUIRE(pl.has_value());
    REQUIRE(pr.has_value());
    CHECK((*pl)[10] == game[10]);
    CHECK((*pl)[11] == game[11]);
    CHECK(approxEqual((*pl)[2], -(*pr)[2])); // mirrored horizontal offsets
    CHECK((*pl)[2] < 0.0f);                  // eye L's frustum reaches further left
    const auto back = fovOfEngineProjection(*pl);
    REQUIRE(back.has_value());
    CHECK(approxEqual(back->angleLeft, leftEye().angleLeft));
    CHECK(approxEqual(back->angleRight, leftEye().angleRight));
    CHECK(approxEqual(back->angleUp, leftEye().angleUp));
    CHECK(approxEqual(back->angleDown, leftEye().angleDown));
}
