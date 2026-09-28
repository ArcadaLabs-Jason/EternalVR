#include "xr_math/head_view.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>

using evr::Quat;
using evr::Vec3;
using evr::test::approxEqual;
using evr::xr_math::composeHeadAxis;
using evr::xr_math::fovFromGame;
using evr::xr_math::gameFovFromTangents;
using evr::xr_math::headOffsetInWorld;
using evr::xr_math::IdViewAxis;
using evr::xr_math::isOrthonormal;
using evr::xr_math::openXrToIdTech;
using evr::xr_math::viewAxisFromQuat;
using evr::xr_math::yawOnly;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

float radians(float degrees) {
    return degrees * kPi / 180.0f;
}

// An id Tech view axis for a heading (yaw, counter-clockwise from +X toward +Y) and pitch (positive
// looks up), as the game builds it.
IdViewAxis gameAxis(float yawDegrees, float pitchDegrees) {
    const Quat yaw = Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, radians(yawDegrees));
    // Pitching up turns forward (+X) toward up (+Z): a negative rotation about +Y (left).
    const Quat pitch = Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, -radians(pitchDegrees));
    return viewAxisFromQuat(yaw * pitch);
}

// Headset orientations in OpenXR axes.
Quat xrYaw(float degrees) {
    return Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, radians(degrees));
}
Quat xrPitch(float degrees) {
    return Quat::fromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, radians(degrees));
}
Quat xrRoll(float degrees) {
    return Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, radians(degrees));
}

bool approxEqual(const IdViewAxis& a, const IdViewAxis& b) {
    return evr::test::approxEqual(a.forward, b.forward) && evr::test::approxEqual(a.left, b.left) &&
           evr::test::approxEqual(a.up, b.up);
}

} // namespace

TEST_CASE("OpenXR directions map onto id Tech axes") {
    // OpenXR forward (-Z), right (+X) and up (+Y) become id Tech forward (+X), right (-Y) and up (+Z).
    CHECK(approxEqual(openXrToIdTech(Vec3{0.0f, 0.0f, -1.0f}), Vec3{1.0f, 0.0f, 0.0f}));
    CHECK(approxEqual(openXrToIdTech(Vec3{1.0f, 0.0f, 0.0f}), Vec3{0.0f, -1.0f, 0.0f}));
    CHECK(approxEqual(openXrToIdTech(Vec3{0.0f, 1.0f, 0.0f}), Vec3{0.0f, 0.0f, 1.0f}));
}

TEST_CASE("an OpenXR orientation converts to the same physical rotation in id Tech axes") {
    const Quat samples[] = {xrYaw(30.0f), xrPitch(-20.0f), xrRoll(15.0f),
                            xrYaw(-70.0f) * xrPitch(25.0f) * xrRoll(-10.0f)};
    const Vec3 vectors[] = {{0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.3f, -0.5f, 0.8f}};
    for (const Quat q : samples) {
        for (const Vec3 v : vectors) {
            // Rotating in OpenXR then converting equals converting then rotating in id Tech.
            CHECK(approxEqual(openXrToIdTech(evr::rotate(q, v)),
                              evr::rotate(openXrToIdTech(q), openXrToIdTech(v))));
        }
    }
}

TEST_CASE("the identity head pose leaves the body view unchanged") {
    const IdViewAxis body = *yawOnly(gameAxis(40.0f, 0.0f));
    CHECK(approxEqual(composeHeadAxis(body, openXrToIdTech(Quat::identity())), body));
}

TEST_CASE("turning the head left turns the view left") {
    // A positive rotation about OpenXR +Y turns -Z toward -X, which is the wearer's left.
    const IdViewAxis body = *yawOnly(gameAxis(0.0f, 0.0f));
    const IdViewAxis view = composeHeadAxis(body, openXrToIdTech(xrYaw(30.0f)));
    CHECK(approxEqual(view.forward, Vec3{std::cos(radians(30.0f)), std::sin(radians(30.0f)), 0.0f}));
    CHECK(approxEqual(view.up, Vec3{0.0f, 0.0f, 1.0f}));
}

TEST_CASE("looking up in the headset pitches the view up") {
    const IdViewAxis body = *yawOnly(gameAxis(0.0f, 0.0f));
    const IdViewAxis view = composeHeadAxis(body, openXrToIdTech(xrPitch(25.0f)));
    CHECK(approxEqual(view.forward, Vec3{std::cos(radians(25.0f)), 0.0f, std::sin(radians(25.0f))}));
    CHECK(approxEqual(view.left, Vec3{0.0f, 1.0f, 0.0f}));
}

TEST_CASE("tilting the head left rolls the view left") {
    // A positive rotation about OpenXR +Z (pointing backwards) tips +Y toward -X: the head leans left.
    const IdViewAxis body = *yawOnly(gameAxis(0.0f, 0.0f));
    const IdViewAxis view = composeHeadAxis(body, openXrToIdTech(xrRoll(20.0f)));
    CHECK(approxEqual(view.forward, Vec3{1.0f, 0.0f, 0.0f}));
    CHECK(approxEqual(view.up, Vec3{0.0f, std::sin(radians(20.0f)), std::cos(radians(20.0f))}));
}

TEST_CASE("the game's yaw turns the body and the head yaw adds on top") {
    const IdViewAxis body = *yawOnly(gameAxis(90.0f, 0.0f));
    const IdViewAxis view = composeHeadAxis(body, openXrToIdTech(xrYaw(30.0f)));
    CHECK(approxEqual(view.forward, Vec3{std::cos(radians(120.0f)), std::sin(radians(120.0f)), 0.0f}));
}

TEST_CASE("the game's pitch is dropped from the body frame") {
    const auto body = yawOnly(gameAxis(10.0f, -40.0f));
    REQUIRE(body.has_value());
    CHECK(approxEqual(*body, gameAxis(10.0f, 0.0f)));
    const IdViewAxis view = composeHeadAxis(*body, openXrToIdTech(xrPitch(15.0f)));
    const IdViewAxis expected = gameAxis(10.0f, 15.0f);
    CHECK(approxEqual(view, expected));
}

TEST_CASE("the body heading survives a game view looking straight down or up") {
    const auto down = yawOnly(gameAxis(60.0f, -90.0f));
    REQUIRE(down.has_value());
    CHECK(approxEqual(down->forward, Vec3{std::cos(radians(60.0f)), std::sin(radians(60.0f)), 0.0f}));
    const auto up = yawOnly(gameAxis(-45.0f, 90.0f));
    REQUIRE(up.has_value());
    CHECK(approxEqual(up->forward, Vec3{std::cos(radians(-45.0f)), std::sin(radians(-45.0f)), 0.0f}));
}

TEST_CASE("degenerate game axes are rejected") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(yawOnly(IdViewAxis{{nan, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}).has_value());
    CHECK_FALSE(yawOnly(IdViewAxis{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}}).has_value());
}

TEST_CASE("composed axes stay orthonormal and right-handed") {
    const IdViewAxis body = *yawOnly(gameAxis(-135.0f, 20.0f));
    const IdViewAxis view =
        composeHeadAxis(body, openXrToIdTech(xrYaw(-50.0f) * xrPitch(35.0f) * xrRoll(12.0f)));
    CHECK(isOrthonormal(view));
    CHECK(isOrthonormal(gameAxis(33.0f, -12.0f)));
    CHECK_FALSE(isOrthonormal(IdViewAxis{{1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}));
}

TEST_CASE("the head position becomes a world offset in the body frame") {
    const IdViewAxis body = *yawOnly(gameAxis(90.0f, 0.0f));
    // 0.1 m forward (-Z), 0.2 m right (+X) and 0.05 m up (+Y) in the tracking space.
    const Vec3 offset = headOffsetInWorld(body, Vec3{0.2f, 0.05f, -0.1f}, 1.0f);
    // Facing +Y in the world: forward is +Y and right is +X.
    CHECK(approxEqual(offset, Vec3{0.2f, 0.1f, 0.05f}));
    const Vec3 scaled = headOffsetInWorld(body, Vec3{0.2f, 0.05f, -0.1f}, 39.37f);
    CHECK(approxEqual(scaled, Vec3{0.2f * 39.37f, 0.1f * 39.37f, 0.05f * 39.37f}, 1e-3f));
}

TEST_CASE("game FOV converts to and from symmetric tangents") {
    const auto game = gameFovFromTangents(1.0f, std::tan(radians(40.0f)));
    REQUIRE(game.has_value());
    CHECK(approxEqual(game->fovX, 90.0f, 1e-3f));
    CHECK(approxEqual(game->fovY, 80.0f, 1e-3f));
    const auto fov = fovFromGame(*game);
    REQUIRE(fov.has_value());
    CHECK(approxEqual(fov->angleLeft, -radians(45.0f)));
    CHECK(approxEqual(fov->angleRight, radians(45.0f)));
    CHECK(approxEqual(fov->angleUp, radians(40.0f)));
    CHECK(approxEqual(fov->angleDown, -radians(40.0f)));
    CHECK_FALSE(gameFovFromTangents(0.0f, 1.0f).has_value());
    CHECK_FALSE(gameFovFromTangents(1.0f, std::numeric_limits<float>::infinity()).has_value());
    CHECK_FALSE(fovFromGame({180.0f, 90.0f}).has_value());
    CHECK_FALSE(fovFromGame({90.0f, 0.0f}).has_value());
}
