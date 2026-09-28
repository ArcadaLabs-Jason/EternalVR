#include "features/menu/panel_pointer.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::menu::beamImage;
using evr::menu::beamQuad;
using evr::menu::cursorPixel;
using evr::menu::CursorPixel;
using evr::menu::dotPose;
using evr::menu::intersectPanel;
using evr::menu::Panel;
using evr::menu::quatFromAxes;
using evr::test::approxEqual;

namespace {

constexpr float kPi = 3.14159265358979f;

// A 2 m x 1 m panel 1.5 m ahead of the origin at 1.6 m height, facing back toward it.
Panel frontPanel() {
    Panel p;
    p.pose.position = {0.0f, 1.6f, -1.5f};
    p.width = 2.0f;
    p.height = 1.0f;
    return p;
}

// An aim pose at `position` pointing `yawDeg` left and `pitchDeg` up of -Z.
Pose aimAt(Vec3 position, float yawDeg, float pitchDeg) {
    Pose pose;
    pose.orientation = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yawDeg * kPi / 180.0f) *
                       Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, pitchDeg * kPi / 180.0f);
    pose.position = position;
    return pose;
}

} // namespace

TEST_CASE("pointer: a ray straight at the panel's centre hits (0.5, 0.5)") {
    const auto hit = intersectPanel(frontPanel(), aimAt({0.0f, 1.6f, 0.0f}, 0.0f, 0.0f));
    REQUIRE(hit);
    CHECK(hit->u == doctest::Approx(0.5f));
    CHECK(hit->v == doctest::Approx(0.5f));
    CHECK(hit->distance == doctest::Approx(1.5f));
    CHECK(approxEqual(hit->point, Vec3{0.0f, 1.6f, -1.5f}));
}

TEST_CASE("pointer: u runs left to right and v top to bottom") {
    // Aimed at a point 0.5 m right and 0.25 m up of the centre.
    const Vec3 origin{0.0f, 1.6f, 0.0f};
    const auto hit = intersectPanel(frontPanel(), origin, Vec3{0.5f, 0.25f, -1.5f});
    REQUIRE(hit);
    CHECK(hit->u == doctest::Approx(0.75f));
    CHECK(hit->v == doctest::Approx(0.25f));
    // The corners.
    const auto topLeft = intersectPanel(frontPanel(), origin, Vec3{-1.0f, 0.5f, -1.5f});
    REQUIRE(topLeft);
    CHECK(topLeft->u == doctest::Approx(0.0f));
    CHECK(topLeft->v == doctest::Approx(0.0f));
    const auto bottomRight = intersectPanel(frontPanel(), origin, Vec3{1.0f, -0.5f, -1.5f});
    REQUIRE(bottomRight);
    CHECK(bottomRight->u == doctest::Approx(1.0f));
    CHECK(bottomRight->v == doctest::Approx(1.0f));
}

TEST_CASE("pointer: an aim pose's -Z is the ray") {
    // 10 degrees left of the centre: tan(10 deg) * 1.5 m left.
    const auto hit = intersectPanel(frontPanel(), aimAt({0.0f, 1.6f, 0.0f}, 10.0f, 0.0f));
    REQUIRE(hit);
    const float left = std::tan(10.0f * kPi / 180.0f) * 1.5f;
    CHECK(hit->u == doctest::Approx(0.5f - left / 2.0f));
    CHECK(hit->v == doctest::Approx(0.5f));
    // 10 degrees up.
    const auto up = intersectPanel(frontPanel(), aimAt({0.0f, 1.6f, 0.0f}, 0.0f, 10.0f));
    REQUIRE(up);
    CHECK(up->v == doctest::Approx(0.5f - std::tan(10.0f * kPi / 180.0f) * 1.5f));
}

TEST_CASE("pointer: misses") {
    const Panel panel = frontPanel();
    // Off the side.
    CHECK_FALSE(intersectPanel(panel, Vec3{0.0f, 1.6f, 0.0f}, Vec3{1.2f, 0.0f, -1.5f}));
    // Pointing away.
    CHECK_FALSE(intersectPanel(panel, aimAt({0.0f, 1.6f, 0.0f}, 180.0f, 0.0f)));
    // Parallel to the panel.
    CHECK_FALSE(intersectPanel(panel, Vec3{0.0f, 1.6f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}));
    // From behind it.
    CHECK_FALSE(intersectPanel(panel, Vec3{0.0f, 1.6f, -3.0f}, Vec3{0.0f, 0.0f, 1.0f}));
    // No direction, no panel.
    CHECK_FALSE(intersectPanel(panel, Vec3{0.0f, 1.6f, 0.0f}, Vec3{}));
    Panel empty = panel;
    empty.width = 0.0f;
    CHECK_FALSE(intersectPanel(empty, aimAt({0.0f, 1.6f, 0.0f}, 0.0f, 0.0f)));
}

TEST_CASE("pointer: a turned and moved panel") {
    // The panel 1.5 m to the head's left (yawed 90 degrees), and the hand pointing left at it.
    Panel panel = frontPanel();
    panel.pose.orientation = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, kPi / 2.0f);
    panel.pose.position = {-1.5f, 1.6f, 0.0f};
    const auto hit = intersectPanel(panel, aimAt({0.0f, 1.6f, 0.0f}, 90.0f, 0.0f));
    REQUIRE(hit);
    CHECK(hit->u == doctest::Approx(0.5f));
    CHECK(hit->v == doctest::Approx(0.5f));
    // Pointing a little toward -Z from there lands right of the centre (the panel's right is -Z now).
    const auto right = intersectPanel(panel, aimAt({0.0f, 1.6f, 0.0f}, 80.0f, 0.0f));
    REQUIRE(right);
    CHECK(right->u > 0.5f);
}

TEST_CASE("pointer: panel coordinates to the game's cursor pixels") {
    CHECK(cursorPixel(0.0f, 0.0f, 2064, 2100) == CursorPixel{0, 0});
    CHECK(cursorPixel(0.5f, 0.5f, 2064, 2100) == CursorPixel{1032, 1050});
    // The far edges stay inside the image.
    CHECK(cursorPixel(1.0f, 1.0f, 2064, 2100) == CursorPixel{2063, 2099});
    CHECK(cursorPixel(1.5f, -0.5f, 100, 50) == CursorPixel{99, 0});
    CHECK(cursorPixel(std::nanf(""), 0.25f, 100, 40) == CursorPixel{0, 10});
    CHECK(cursorPixel(0.5f, 0.5f, 0, 0) == CursorPixel{0, 0});
}

TEST_CASE("pointer: the dot sits on the panel, a little in front") {
    const Panel panel = frontPanel();
    const Pose dot = dotPose(panel, 0.75f, 0.25f, 0.01f);
    CHECK(approxEqual(dot.position, Vec3{0.5f, 1.85f, -1.49f}));
    CHECK(dot.orientation == panel.pose.orientation);
}

TEST_CASE("pointer: quatFromAxes rebuilds a rotation") {
    const Quat q = Quat::fromAxisAngle(Vec3{0.3f, 1.0f, -0.2f}, 1.1f);
    const Quat back = quatFromAxes(rotate(q, {1.0f, 0.0f, 0.0f}), rotate(q, {0.0f, 1.0f, 0.0f}),
                                   rotate(q, {0.0f, 0.0f, 1.0f}));
    // q and -q are the same rotation.
    const float sign = (back.w * q.w + back.x * q.x + back.y * q.y + back.z * q.z) < 0.0f ? -1.0f : 1.0f;
    CHECK(back.x * sign == doctest::Approx(q.x).epsilon(1e-4));
    CHECK(back.y * sign == doctest::Approx(q.y).epsilon(1e-4));
    CHECK(back.z * sign == doctest::Approx(q.z).epsilon(1e-4));
    CHECK(back.w * sign == doctest::Approx(q.w).epsilon(1e-4));
    // Half-turns take the other branches.
    for (const Vec3 axis : {Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}}) {
        const Quat h = Quat::fromAxisAngle(axis, kPi);
        const Quat r = quatFromAxes(rotate(h, {1.0f, 0.0f, 0.0f}), rotate(h, {0.0f, 1.0f, 0.0f}),
                                    rotate(h, {0.0f, 0.0f, 1.0f}));
        CHECK(approxEqual(rotate(r, {0.3f, 0.5f, 0.7f}), rotate(h, {0.3f, 0.5f, 0.7f})));
    }
}

TEST_CASE("pointer: the beam runs from the hand to the hit and faces the eye") {
    const Vec3 hand{0.2f, 1.2f, -0.3f};
    const Vec3 hit{0.0f, 1.6f, -1.5f};
    const Vec3 eye{0.0f, 1.6f, 0.0f};
    const auto beam = beamQuad(hand, hit, eye, 0.004f);
    REQUIRE(beam);
    CHECK(beam->length == doctest::Approx(length(hit - hand)));
    CHECK(beam->width == doctest::Approx(0.004f));
    CHECK(approxEqual(beam->pose.position, (hand + hit) * 0.5f));
    // Its +Y runs along the beam, its ends are the two points.
    CHECK(approxEqual(transformPoint(beam->pose, {0.0f, beam->length * 0.5f, 0.0f}), hit));
    CHECK(approxEqual(transformPoint(beam->pose, {0.0f, -beam->length * 0.5f, 0.0f}), hand));
    // Its face (+Z) leans toward the eye and is square to the beam.
    const Vec3 z = rotate(beam->pose.orientation, {0.0f, 0.0f, 1.0f});
    CHECK(dot(z, eye - beam->pose.position) > 0.0f);
    CHECK(dot(z, normalize(hit - hand)) == doctest::Approx(0.0f).epsilon(1e-4));
    // The eye on the beam's line still gives a beam; two equal points do not.
    CHECK(beamQuad(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -2.0f}, Vec3{0.0f, 0.0f, 1.0f}, 0.004f));
    CHECK_FALSE(beamQuad(hand, hand, eye, 0.004f));
    CHECK_FALSE(beamQuad(hand, hit, eye, 0.0f));
}

TEST_CASE("pointer: the beam image is premultiplied and fades to its far end") {
    const std::uint32_t w = 8;
    const std::uint32_t h = 32;
    const auto px = beamImage(w, h);
    REQUIRE(px.size() == static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        CHECK(px[i] <= px[i + 3]);
        CHECK(px[i + 1] <= px[i + 3]);
        CHECK(px[i + 2] <= px[i + 3]);
    }
    const auto alpha = [&](std::uint32_t x, std::uint32_t y) {
        return px[(static_cast<std::size_t>(y) * w + x) * 4 + 3];
    };
    // Brighter at the hand (bottom row) than at the far end (top row), strongest in the middle columns.
    CHECK(alpha(w / 2, h - 1) > alpha(w / 2, 0));
    CHECK(alpha(w / 2, h / 2) > alpha(0, h / 2));
    CHECK(beamImage(0, 4).empty());
}
