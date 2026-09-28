#include "xr_math/stereo_view.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::test::approxEqual;
using evr::xr_math::copiesFirstViewJitter;
using evr::xr_math::EngineMatrix;
using evr::xr_math::engineProjection;
using evr::xr_math::eyePoseInSpace;
using evr::xr_math::eyeViewInWorld;
using evr::xr_math::Fov;
using evr::xr_math::fovOfEngineProjection;
using evr::xr_math::IdViewAxis;
using evr::xr_math::isEnginePerspective;
using evr::xr_math::sideBySideRect;
using evr::xr_math::viewAxisFromQuat;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

float radians(float degrees) {
    return degrees * kPi / 180.0f;
}

// The engine's own builder (RVA 0x39A310) for a symmetric FOV: the depth rows the eye matrices reuse.
EngineMatrix engineBuilder(float fovXDegrees, float fovYDegrees, float zNear, float zFar) {
    const float tx = std::tan(radians(fovXDegrees) * 0.5f);
    const float ty = std::tan(radians(fovYDegrees) * 0.5f);
    EngineMatrix m{};
    m[0] = 1.0f / tx;
    m[5] = 1.0f / ty;
    m[10] = -zFar / (zFar - zNear);
    m[11] = -(zNear * zFar) / (zFar - zNear);
    m[14] = -1.0f;
    return m;
}

// The Quest 3 left eye through VDXR (VR_HEAD_TRACKED.md): 54 degrees temporal, 40 nasal, 44 up, 55 down.
Fov questLeft() {
    return {radians(-54.0f), radians(40.0f), radians(44.0f), radians(-55.0f)};
}

// The engine's view space (RVA 0x39A490): x = right (-left), y = up, z = -forward.
Vec3 engineViewSpace(Vec3 origin, const IdViewAxis& axis, Vec3 point) {
    const Vec3 d = point - origin;
    return {-evr::dot(axis.left, d), evr::dot(axis.up, d), -evr::dot(axis.forward, d)};
}

// Normalised device x and y of a view-space point through an engine matrix.
std::array<float, 2> engineNdc(const EngineMatrix& m, Vec3 v) {
    const float x = m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3];
    const float y = m[4] * v.x + m[5] * v.y + m[6] * v.z + m[7];
    const float w = m[12] * v.x + m[13] * v.y + m[14] * v.z + m[15];
    return {x / w, y / w};
}

// The same point as the OpenXR side sees it: in the eye's space (x right, y up, -z forward), mapped
// through the eye's FOV to [-1, 1].
std::array<float, 2> openXrNdc(const Pose& eyeInSpace, const Fov& fov, Vec3 pointInSpace) {
    const Vec3 p = evr::transformPoint(evr::inverse(eyeInSpace), pointInSpace);
    const float tx = p.x / -p.z;
    const float ty = p.y / -p.z;
    const float l = std::tan(fov.angleLeft);
    const float r = std::tan(fov.angleRight);
    const float d = std::tan(fov.angleDown);
    const float u = std::tan(fov.angleUp);
    return {2.0f * (tx - l) / (r - l) - 1.0f, 2.0f * (ty - d) / (u - d) - 1.0f};
}

IdViewAxis identityBody() {
    return {};
}

} // namespace

TEST_CASE("each eye sits half the IPD to its side of the head") {
    const Pose left{Quat::identity(), Vec3{-0.032f, 0.0f, 0.0f}};
    const Pose right{Quat::identity(), Vec3{0.032f, 0.0f, 0.0f}};
    const auto l = eyeViewInWorld(identityBody(), Quat::identity(), left, 1.0f);
    const auto r = eyeViewInWorld(identityBody(), Quat::identity(), right, 1.0f);
    // id Tech +Y is left.
    CHECK(approxEqual(l.offset, Vec3{0.0f, 0.032f, 0.0f}));
    CHECK(approxEqual(r.offset, Vec3{0.0f, -0.032f, 0.0f}));
    CHECK(approxEqual(l.axis.forward, Vec3{1.0f, 0.0f, 0.0f}));
    CHECK(approxEqual(r.axis.left, Vec3{0.0f, 1.0f, 0.0f}));
}

TEST_CASE("the eye offset turns with the head and the body, and scales with the world") {
    const Pose left{Quat::identity(), Vec3{-0.032f, 0.0f, 0.0f}};
    // Head turned 90 degrees to the left (OpenXR yaw about +Y): the left eye is now behind the centre.
    const Quat headLeft = Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, radians(90.0f));
    const auto turned = eyeViewInWorld(identityBody(), headLeft, left, 1.0f);
    CHECK(approxEqual(turned.offset, Vec3{-0.032f, 0.0f, 0.0f}));
    CHECK(approxEqual(turned.axis.forward, Vec3{0.0f, 1.0f, 0.0f}));

    // Body facing +Y (yaw 90 in the world), head straight: the left eye is toward world -X.
    IdViewAxis body;
    body.forward = {0.0f, 1.0f, 0.0f};
    body.left = {-1.0f, 0.0f, 0.0f};
    body.up = {0.0f, 0.0f, 1.0f};
    const auto yawed = eyeViewInWorld(body, Quat::identity(), left, 2.0f);
    CHECK(approxEqual(yawed.offset, Vec3{-0.064f, 0.0f, 0.0f}));
}

TEST_CASE("a canted eye turns its view axis, not only its origin") {
    const Pose canted{Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, radians(10.0f)), Vec3{-0.032f, 0.0f, 0.0f}};
    const auto eye = eyeViewInWorld(identityBody(), Quat::identity(), canted, 1.0f);
    // Ten degrees outward (to the left) from +X toward +Y.
    CHECK(approxEqual(eye.axis.forward, Vec3{std::cos(radians(10.0f)), std::sin(radians(10.0f)), 0.0f}));
    CHECK(evr::xr_math::isOrthonormal(eye.axis));
}

TEST_CASE("the eye pose in the tracking space composes the head pose") {
    const Pose head{Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, radians(90.0f)), Vec3{0.0f, 1.7f, 0.0f}};
    const Pose eye = eyePoseInSpace(head, Pose{Quat::identity(), Vec3{-0.032f, 0.0f, 0.0f}});
    CHECK(approxEqual(eye.position, Vec3{0.0f, 1.7f, 0.032f}));
    CHECK(approxEqual(eye.orientation.w, head.orientation.w));
}

TEST_CASE("the eye's pose in head space comes from head and eye located in the same space") {
    const Pose head{Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, radians(30.0f)), Vec3{0.2f, 1.7f, -0.1f}};
    const Pose eyeInHead{Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, radians(5.0f)),
                         Vec3{-0.032f, 0.0f, 0.01f}};
    const Pose eyeInSpace = eyePoseInSpace(head, eyeInHead);
    const Pose back = evr::xr_math::eyeInHeadFromSpace(head, eyeInSpace);
    CHECK(approxEqual(back.position, eyeInHead.position));
    CHECK(approxEqual(back.orientation.y, eyeInHead.orientation.y));
    CHECK(evr::xr_math::plausibleEyeInHead(back));
    // An eye pose that still carries the head's height is not an eye in head space.
    CHECK_FALSE(evr::xr_math::plausibleEyeInHead(Pose{Quat::identity(), Vec3{-0.032f, 1.7f, 0.0f}}));
}

TEST_CASE("a symmetric FOV reproduces the engine's own matrix") {
    const EngineMatrix engine = engineBuilder(108.0f, 110.0f, 0.1f, 10000.0f);
    REQUIRE(isEnginePerspective(engine));
    const Fov symmetric{radians(-54.0f), radians(54.0f), radians(55.0f), radians(-55.0f)};
    const auto m = engineProjection(symmetric, engine);
    REQUIRE(m);
    for (std::size_t i = 0; i < 16; ++i) {
        CHECK(approxEqual((*m)[i], engine[i]));
    }
}

TEST_CASE("an asymmetric eye matrix maps the eye's frustum edges to the image edges") {
    const EngineMatrix depth = engineBuilder(90.0f, 90.0f, 0.1f, 10000.0f);
    const Fov fov = questLeft();
    const auto m = engineProjection(fov, depth);
    REQUIRE(m);
    const float z = -5.0f;
    const auto atRight = engineNdc(*m, {std::tan(fov.angleRight) * 5.0f, 0.0f, z});
    const auto atLeft = engineNdc(*m, {std::tan(fov.angleLeft) * 5.0f, 0.0f, z});
    const auto atUp = engineNdc(*m, {0.0f, std::tan(fov.angleUp) * 5.0f, z});
    const auto atDown = engineNdc(*m, {0.0f, std::tan(fov.angleDown) * 5.0f, z});
    CHECK(approxEqual(atRight[0], 1.0f));
    CHECK(approxEqual(atLeft[0], -1.0f));
    CHECK(approxEqual(atUp[1], 1.0f));
    CHECK(approxEqual(atDown[1], -1.0f));
    // Straight ahead lands right of centre for the left eye (it sees more to its temporal side).
    CHECK(engineNdc(*m, {0.0f, 0.0f, z})[0] > 0.2f);
    // Depth rows come from the engine's matrix unchanged.
    for (std::size_t i = 8; i < 16; ++i) {
        CHECK((*m)[i] == depth[i]);
    }
    const auto back = fovOfEngineProjection(*m);
    REQUIRE(back);
    CHECK(approxEqual(back->angleLeft, fov.angleLeft));
    CHECK(approxEqual(back->angleRight, fov.angleRight));
    CHECK(approxEqual(back->angleUp, fov.angleUp));
    CHECK(approxEqual(back->angleDown, fov.angleDown));
}

TEST_CASE("the engine's eye view and the projection layer's eye agree on where a point appears") {
    // A head turned and pitched, the left and right eyes of a 64 mm IPD, points at several depths: the
    // point's position in the eye image must be the same whether it goes through the engine's view
    // (origin, axis, explicit matrix) or through the OpenXR eye pose and FOV the layer submits.
    const float scale = 1.0f;
    const Quat head = Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, radians(25.0f)) *
                      Quat::fromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, radians(-10.0f));
    const Pose headPose{head, Vec3{}};
    IdViewAxis body = viewAxisFromQuat(Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, radians(30.0f)));
    const Vec3 worldOrigin{10.0f, -4.0f, 1.6f}; // where the game puts the head
    const EngineMatrix depth = engineBuilder(108.0f, 110.0f, 0.1f, 10000.0f);
    const Fov fovs[2] = {questLeft(), {radians(-40.0f), radians(54.0f), radians(44.0f), radians(-55.0f)}};
    const Pose eyes[2] = {{Quat::identity(), Vec3{-0.032f, 0.0f, 0.0f}},
                          {Quat::identity(), Vec3{0.032f, 0.0f, 0.0f}}};
    const Vec3 points[] = {{0.3f, 0.1f, -0.5f}, {-1.0f, -0.4f, -3.0f}, {2.0f, 1.5f, -20.0f}};
    for (int e = 0; e < 2; ++e) {
        const auto view = eyeViewInWorld(body, head, eyes[e], scale);
        const auto m = engineProjection(fovs[e], depth);
        REQUIRE(m);
        const Pose eyeInSpace = eyePoseInSpace(headPose, eyes[e]);
        for (const Vec3& p : points) {
            // The tracking space maps into the world through the body frame at the head's origin.
            const Vec3 pid = evr::xr_math::openXrToIdTech(p) * scale;
            const Vec3 world = worldOrigin + body.forward * pid.x + body.left * pid.y + body.up * pid.z;
            const auto engine = engineNdc(*m, engineViewSpace(worldOrigin + view.offset, view.axis, world));
            const auto xr = openXrNdc(eyeInSpace, fovs[e], p);
            CHECK(approxEqual(engine[0], xr[0], 1e-3f));
            CHECK(approxEqual(engine[1], xr[1], 1e-3f));
        }
    }
}

TEST_CASE("the eye matrix refuses bad input") {
    const EngineMatrix depth = engineBuilder(90.0f, 90.0f, 0.1f, 10000.0f);
    EngineMatrix notPerspective = depth;
    notPerspective[14] = 0.0f;
    CHECK_FALSE(engineProjection(questLeft(), notPerspective));
    EngineMatrix identity{};
    identity[0] = identity[5] = identity[10] = identity[15] = 1.0f;
    CHECK_FALSE(isEnginePerspective(identity));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(engineProjection(Fov{nan, 0.5f, 0.5f, -0.5f}, depth));
    CHECK_FALSE(engineProjection(Fov{0.5f, 0.5f, 0.5f, -0.5f}, depth));   // no width
    CHECK_FALSE(engineProjection(Fov{-1.57f, 0.5f, 0.5f, -0.5f}, depth)); // edge at 90 degrees
}

TEST_CASE("the side-by-side rectangles follow the engine's rounding") {
    const auto l = sideBySideRect(0, 3840, 2100);
    const auto r = sideBySideRect(1, 3840, 2100);
    CHECK(l.x == 0);
    CHECK(l.width == 1920);
    CHECK(r.x == 1920);
    CHECK(r.width == 1920);
    CHECK(r.height == 2100);
    const auto oddL = sideBySideRect(0, 2561, 1000);
    const auto oddR = sideBySideRect(1, 2561, 1000);
    CHECK(oddL.width == 1280);
    CHECK(oddR.x == 1280);
    CHECK(oddR.width == 1281);
}

TEST_CASE("only the views after the first take the first view's TAA jitter") {
    CHECK_FALSE(copiesFirstViewJitter(0));
    CHECK(copiesFirstViewJitter(1));
}
