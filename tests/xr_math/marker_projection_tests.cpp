#include "xr_math/marker_projection.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

using evr::test::approxEqual;
using evr::xr_math::EngineMatrix;
using evr::xr_math::GuiPoint;
using evr::xr_math::isEnginePerspective;
using evr::xr_math::markerOffFrame;
using evr::xr_math::markerProjection;
using evr::xr_math::MarkerQuad;
using evr::xr_math::markerTargetFov;
using evr::xr_math::projectToScreen;
using evr::xr_math::quadPixelOfTangents;
using evr::xr_math::tangentsOfQuadPixel;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

float radians(float degrees) {
    return degrees * kPi / 180.0f;
}

float degrees(float radians) {
    return radians * 180.0f / kPi;
}

// The engine's own builder (RVA 0x39A310) for a symmetric FOV (stereo_view_tests.cpp).
EngineMatrix engineBuilder(float fovXDegrees, float fovYDegrees, float zNear, float zFar) {
    EngineMatrix m{};
    m[0] = 1.0f / std::tan(radians(fovXDegrees) * 0.5f);
    m[5] = 1.0f / std::tan(radians(fovYDegrees) * 0.5f);
    m[10] = -zFar / (zFar - zNear);
    m[11] = -(zNear * zFar) / (zFar - zNear);
    m[14] = -1.0f;
    return m;
}

// The head-tracked view's projection on a Quest 3 (54 x 54.27 degrees each side, docs/VR_STEREO.md).
EngineMatrix questHeadView() {
    return engineBuilder(108.0f, 108.54f, 3.0f, 100000.0f);
}

// The default UI quad (2.0 m wide at 1.5 m, no offset) on the simulator's near-square GUI target (1280 x
// 1400, the render size `auto` gives there) showing its 16:9 band (ui_layer::wideContentRect: 1280 x 720 from
// row 340).
MarkerQuad defaultQuad() {
    MarkerQuad q;
    q.widthMetres = 2.0f;
    q.distanceMetres = 1.5f;
    q.offsetYMetres = 0.0f;
    q.targetWidth = 1280;
    q.targetHeight = 1400;
    q.shownX = 0;
    q.shownY = 340;
    q.shownWidth = 1280;
    q.shownHeight = 720;
    return q;
}

// A point 5 units ahead of the head in the direction with these tangents, in the engine's view space (x
// right, y up, z back).
struct ViewPoint {
    float x, y, z;
};
ViewPoint ahead(float right, float up) {
    return {right * 5.0f, up * 5.0f, -5.0f};
}

// The angle between the directions with tangents (r0, u0) and (r1, u1), in degrees.
// (atan2 of the cross and dot products, in double: an acos of a float near 1 resolves only about 0.03
// degrees.)
float angleBetween(float r0, float u0, float r1, float u1) {
    const double a[3] = {r0, u0, 1.0};
    const double b[3] = {r1, u1, 1.0};
    const double cross[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    const double sine = std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
    const double cosine = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    return static_cast<float>(std::atan2(sine, cosine) * 180.0 / std::numbers::pi);
}

// Where the HUD draws the direction (`right`, `up`) through `m` on the quad's GUI target, as the direction
// the quad shows that pixel in: the angle from the true direction, in degrees.
float markerError(const MarkerQuad& q, const EngineMatrix& m, float right, float up) {
    const ViewPoint p = ahead(right, up);
    const auto pixel = projectToScreen(m, p.x, p.y, p.z, static_cast<float>(q.targetWidth),
                                       static_cast<float>(q.targetHeight));
    REQUIRE(pixel);
    const auto shown = tangentsOfQuadPixel(q, *pixel);
    REQUIRE(shown);
    return angleBetween(right, up, shown->right, shown->up);
}

} // namespace

TEST_CASE("a marker lands where the quad shows its target, out to the band's edges") {
    const MarkerQuad q = defaultQuad();
    const auto m = markerProjection(q, questHeadView());
    REQUIRE(m);
    // Across the band (33.7 degrees each side) and up and down it (20.6 degrees).
    for (const float yaw : {-33.0f, -25.0f, -15.0f, -5.0f, 0.0f, 5.0f, 15.0f, 25.0f, 33.0f}) {
        for (const float pitch : {-20.0f, -10.0f, 0.0f, 10.0f, 20.0f}) {
            const float right = std::tan(radians(yaw));
            const float up = std::tan(radians(pitch));
            CAPTURE(yaw);
            CAPTURE(pitch);
            CHECK(markerError(q, *m, right, up) < 0.01f);
            // The same pixel as the ray through the quad's plane.
            const ViewPoint p = ahead(right, up);
            const auto pixel = projectToScreen(*m, p.x, p.y, p.z, 1280.0f, 1400.0f);
            const auto onQuad = quadPixelOfTangents(q, right, up);
            REQUIRE(pixel);
            REQUIRE(onQuad);
            CHECK(approxEqual(pixel->x, onQuad->x, 0.02f));
            CHECK(approxEqual(pixel->y, onQuad->y, 0.02f));
        }
    }
}

TEST_CASE("the game's own projection draws a marker at about half its target's angle") {
    // What the HUD did before: the head view's 108 degrees mapped over the whole GUI target, which the quad
    // shows 67 degrees wide. A target 20 degrees right is drawn 10 degrees right.
    const MarkerQuad q = defaultQuad();
    const ViewPoint p = ahead(std::tan(radians(20.0f)), 0.0f);
    const auto pixel = projectToScreen(questHeadView(), p.x, p.y, p.z, 1280.0f, 1400.0f);
    REQUIRE(pixel);
    const auto shown = tangentsOfQuadPixel(q, *pixel);
    REQUIRE(shown);
    CHECK(degrees(std::atan(shown->right)) == doctest::Approx(10.0f).epsilon(0.01));
    CHECK(markerError(q, questHeadView(), std::tan(radians(30.0f)), 0.0f) > 10.0f);
}

TEST_CASE("the quad's frustum: the band's edges at the quad's edges, the whole target beyond them") {
    const auto fov = markerTargetFov(defaultQuad());
    REQUIRE(fov);
    // 1.0 m either side at 1.5 m; the band's 0.5625 m half height is 360 of the target's 700 half rows.
    CHECK(degrees(fov->angleRight) == doctest::Approx(degrees(std::atan(1.0f / 1.5f))).epsilon(1e-4));
    CHECK(degrees(fov->angleLeft) == doctest::Approx(-degrees(std::atan(1.0f / 1.5f))).epsilon(1e-4));
    const float up = 0.5625f * 700.0f / 360.0f / 1.5f;
    CHECK(degrees(fov->angleUp) == doctest::Approx(degrees(std::atan(up))).epsilon(1e-4));
    CHECK(degrees(fov->angleDown) == doctest::Approx(-degrees(std::atan(up))).epsilon(1e-4));
    // The band's top row is the quad's top edge.
    const auto top = tangentsOfQuadPixel(defaultQuad(), {640.0f, 340.0f});
    REQUIRE(top);
    CHECK(approxEqual(top->up, 0.5625f / 1.5f));
    CHECK(approxEqual(top->right, 0.0f));
}

TEST_CASE("a quad moved down, a wider one, or one showing the whole target keeps the markers on target") {
    MarkerQuad lowered = defaultQuad();
    lowered.offsetYMetres = -0.15f;
    MarkerQuad wide = defaultQuad();
    wide.widthMetres = 2.6f;
    wide.distanceMetres = 1.2f;
    MarkerQuad whole = defaultQuad();
    whole.shownY = 0;
    whole.shownHeight = 1400;
    for (const MarkerQuad& q : {lowered, wide, whole}) {
        const auto m = markerProjection(q, questHeadView());
        REQUIRE(m);
        for (const float yaw : {-30.0f, 0.0f, 12.0f, 30.0f}) {
            for (const float pitch : {-18.0f, -5.0f, 0.0f, 15.0f}) {
                CAPTURE(yaw);
                CAPTURE(pitch);
                CHECK(markerError(q, *m, std::tan(radians(yaw)), std::tan(radians(pitch))) < 0.01f);
            }
        }
    }
    // Lowered by 0.15 m: straight ahead is drawn above the band's middle row, by 0.15 m of its 1.125 m.
    const auto ahead0 = quadPixelOfTangents(lowered, 0.0f, 0.0f);
    REQUIRE(ahead0);
    CHECK(approxEqual(ahead0->y, 340.0f + 360.0f - 0.15f / 1.125f * 720.0f, 0.01f));
}

TEST_CASE("pixels and directions round-trip through the quad") {
    const MarkerQuad q = defaultQuad();
    for (const GuiPoint p : {GuiPoint{0.0f, 0.0f}, GuiPoint{640.0f, 700.0f}, GuiPoint{1280.0f, 1400.0f},
                             GuiPoint{97.5f, 412.25f}}) {
        const auto t = tangentsOfQuadPixel(q, p);
        REQUIRE(t);
        const auto back = quadPixelOfTangents(q, t->right, t->up);
        REQUIRE(back);
        CHECK(approxEqual(back->x, p.x, 1e-3f));
        CHECK(approxEqual(back->y, p.y, 1e-3f));
    }
}

TEST_CASE("the depth and w rows stay the game's") {
    const EngineMatrix engine = questHeadView();
    const auto m = markerProjection(defaultQuad(), engine);
    REQUIRE(m);
    REQUIRE(isEnginePerspective(*m));
    for (std::size_t i = 8; i < 16; ++i) {
        CHECK((*m)[i] == engine[i]);
    }
    CHECK((*m)[1] == 0.0f);
    CHECK((*m)[4] == 0.0f);
}

TEST_CASE("no projection for a quad that cannot show the target or a matrix the engine did not build") {
    const EngineMatrix engine = questHeadView();
    MarkerQuad noWidth = defaultQuad();
    noWidth.widthMetres = 0.0f;
    MarkerQuad behind = defaultQuad();
    behind.distanceMetres = -1.5f;
    MarkerQuad outside = defaultQuad();
    outside.shownY = 900;
    MarkerQuad empty = defaultQuad();
    empty.targetWidth = 0;
    for (const MarkerQuad& q : {noWidth, behind, outside, empty}) {
        CHECK_FALSE(markerProjection(q, engine));
        CHECK_FALSE(markerTargetFov(q));
    }
    EngineMatrix orthographic = engine;
    orthographic[14] = 0.0f;
    orthographic[15] = 1.0f;
    CHECK_FALSE(markerProjection(defaultQuad(), orthographic));
}

TEST_CASE("a marker drawn above or below the band is off the frame although the game's own test passed it") {
    // The POI update (RVA 0xEEE970): the projector onto the SWF's 1920 x 1080 frame, the game's test there,
    // then the stretch of y by (16 / 9) / (the target's aspect) about the frame's middle, which on the
    // near-square target maps the projection's full height to the target's and the frame to the band.
    const MarkerQuad q = defaultQuad();
    const auto m = markerProjection(q, questHeadView());
    REQUIRE(m);
    const float width = 1920.0f;
    const float height = 1080.0f;
    const float stretch = (width / height) / (1280.0f / 1400.0f);
    const auto drawnAt = [&](float pitch, bool& gameOff) {
        const ViewPoint p = ahead(0.0f, std::tan(radians(pitch)));
        const auto s = projectToScreen(*m, p.x, p.y, p.z, width, height);
        REQUIRE(s);
        gameOff = !(s->x >= 0.0f && s->x <= width && s->y >= 0.0f && s->y <= height);
        return GuiPoint{s->x, 0.5f * height + stretch * (s->y - 0.5f * height)};
    };
    bool gameOff = true;
    // 25 degrees up: past the band's 20.6 degrees, inside the target's 36.1.
    GuiPoint drawn = drawnAt(25.0f, gameOff);
    CHECK_FALSE(gameOff);
    CHECK(drawn.y < 0.0f);
    CHECK(markerOffFrame(gameOff, drawn, width, height));
    drawn = drawnAt(-30.0f, gameOff);
    CHECK_FALSE(gameOff);
    CHECK(markerOffFrame(gameOff, drawn, width, height));
    // 15 degrees up is on the band and stays on the frame.
    drawn = drawnAt(15.0f, gameOff);
    CHECK_FALSE(markerOffFrame(gameOff, drawn, width, height));
    // Off the frame by the game's own test stays off.
    CHECK(markerOffFrame(true, {960.0f, 540.0f}, width, height));
    CHECK(markerOffFrame(false, {-1.0f, 540.0f}, width, height));
    CHECK(markerOffFrame(false, {960.0f, std::nanf("")}, width, height));
}
