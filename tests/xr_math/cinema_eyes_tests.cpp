#include "xr_math/cinema_eyes.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

using namespace evr::xr_math;

namespace {

// Where a point `forward` ahead of the camera and `left` to its left lands in one eye's image, as a fraction
// of the image width from its left edge, worked out from the eye's frustum alone.
float imageFraction(const CinemaEyeView& eye, float forward, float left) {
    const FovTangents t = toTangents(eye.fov);
    const float lateralRight = -(left - eye.leftUnits); // the eye sits eye.leftUnits to the left
    const float tanAngle = lateralRight / forward;
    return (tanAngle - t.left) / (t.right - t.left);
}

float measuredDisparity(const CinemaStereo& s, float forward, float left) {
    return imageFraction(s.eyes[1], forward, left) - imageFraction(s.eyes[0], forward, left);
}

CinemaStereoInput wide(float interaxial = 0.063f) {
    CinemaStereoInput in;
    in.interaxialMetres = interaxial;
    in.fovXDegrees = 95.0f;
    in.fovYDegrees = 63.09f;
    return in;
}

} // namespace

TEST_CASE("the eyes sit half the separation either side of the camera, the left eye on the left") {
    const auto s = cinemaStereo(wide(0.04f));
    REQUIRE(s.has_value());
    CHECK(s->eyes[0].leftUnits == doctest::Approx(0.02f));
    CHECK(s->eyes[1].leftUnits == doctest::Approx(-0.02f));
    const auto scaled = [] {
        CinemaStereoInput in = wide(0.04f);
        in.unitsPerMetre = 40.0f;
        return cinemaStereo(in);
    }();
    REQUIRE(scaled.has_value());
    CHECK(scaled->eyes[0].leftUnits == doctest::Approx(0.8f));
}

TEST_CASE("the frusta are shifted opposite ways and keep the view's size") {
    const auto s = cinemaStereo(wide(0.04f));
    REQUIRE(s.has_value());
    const FovTangents l = toTangents(s->eyes[0].fov);
    const FovTangents r = toTangents(s->eyes[1].fov);
    const float tanX = std::tan(95.0f * 0.5f * 3.14159265f / 180.0f);
    CHECK(l.right - l.left == doctest::Approx(2.0f * tanX).epsilon(1e-4));
    CHECK(r.right - r.left == doctest::Approx(2.0f * tanX).epsilon(1e-4));
    CHECK(l.right + l.left == doctest::Approx(2.0f * tanX * kCinemaInfinityShift).epsilon(1e-3));
    CHECK(r.right + r.left == doctest::Approx(-2.0f * tanX * kCinemaInfinityShift).epsilon(1e-3));
    CHECK(l.up == doctest::Approx(r.up));
    CHECK(l.down == doctest::Approx(r.down));
}

TEST_CASE(
    "measured screen disparity: infinity behind the screen, the zero plane on it, near objects crossed") {
    const auto s = cinemaStereo(wide(0.04f));
    REQUIRE(s.has_value());
    CHECK_FALSE(s->clamped);
    // Far away: the shift.
    CHECK(measuredDisparity(*s, 1.0e6f, 0.0f) == doctest::Approx(kCinemaInfinityShift).epsilon(1e-3));
    // On the zero-parallax plane: none.
    CHECK(measuredDisparity(*s, s->zeroParallaxMetres, 0.0f) == doctest::Approx(0.0f).epsilon(1e-3));
    // Nearer than that: crossed (negative), growing as the object comes closer.
    const float at1 = measuredDisparity(*s, 1.0f, 0.0f);
    const float at05 = measuredDisparity(*s, 0.5f, 0.0f);
    CHECK(at1 < 0.0f);
    CHECK(at05 < at1);
    // The closed form agrees with the projection through the frusta, off the centre line too.
    const float tanX = std::tan(95.0f * 0.5f * 3.14159265f / 180.0f);
    CHECK(at1 == doctest::Approx(cinemaDisparity(s->interaxialMetres, tanX, kCinemaInfinityShift, 1.0f))
                     .epsilon(1e-3));
    CHECK(measuredDisparity(*s, 1.0f, 0.4f) == doctest::Approx(at1).epsilon(1e-3));
    // Beyond the zero plane: behind the screen.
    CHECK(measuredDisparity(*s, 3.0f * s->zeroParallaxMetres, 0.0f) > 0.0f);
}

TEST_CASE("the zero-parallax distance matches the geometry") {
    const float tanX = std::tan(95.0f * 0.5f * 3.14159265f / 180.0f);
    // 65 mm, 95 degrees, 1%: about 3 m (the design's worked value).
    CHECK(cinemaZeroParallax(0.065f, tanX, kCinemaInfinityShift) == doctest::Approx(2.99f).epsilon(0.02));
}

TEST_CASE("the comfort clamp keeps the assumed nearest distance within the crossed limit") {
    for (const float fovY : {27.0f, 40.0f, 59.0f, 63.09f}) {
        CinemaStereoInput in;
        in.interaxialMetres = 0.2f; // far more than any shot allows
        in.fovYDegrees = fovY;
        in.fovXDegrees = 2.0f * std::atan(std::tan(fovY * 0.5f * 3.14159265f / 180.0f) * 16.0f / 9.0f) *
                         180.0f / 3.14159265f;
        const auto s = cinemaStereo(in);
        REQUIRE(s.has_value());
        CHECK(s->clamped);
        CHECK(s->interaxialMetres < 0.2f);
        CHECK(s->nearestMetres == doctest::Approx(kCinemaNearestMetres));
        CHECK(s->nearestDisparity >= -kCinemaMaxCrossed - 1e-4f);
        CHECK(s->nearestDisparity == doctest::Approx(-kCinemaMaxCrossed).epsilon(1e-2));
        // And measured through the frusta at the assumed nearest distance.
        CHECK(measuredDisparity(*s, s->nearestMetres, 0.0f) >= -kCinemaMaxCrossed - 1e-4f);
    }
}

TEST_CASE("without the clamp the same camera leaves the comfort range (the control for the clamp)") {
    CinemaStereoInput in = wide(0.2f);
    const auto s = cinemaStereo(in);
    REQUIRE(s.has_value());
    const float tanX = std::tan(95.0f * 0.5f * 3.14159265f / 180.0f);
    // What the unclamped 200 mm separation would give at the nearest assumed distance.
    const float unclamped = cinemaDisparity(0.2f, tanX, kCinemaInfinityShift, s->nearestMetres);
    CHECK(unclamped < -kCinemaMaxCrossed);
    CHECK(s->nearestDisparity >= -kCinemaMaxCrossed - 1e-4f);
}

TEST_CASE("a separation already inside the limit is used as given") {
    CinemaStereoInput in;
    in.interaxialMetres = 0.01f;
    in.fovXDegrees = 60.0f;
    in.fovYDegrees = 36.0f;
    const auto s = cinemaStereo(in);
    REQUIRE(s.has_value());
    CHECK_FALSE(s->clamped);
    CHECK(s->interaxialMetres == doctest::Approx(0.01f));
}

TEST_CASE("the separation limit follows the lens's half-width and the zero-parallax plane is the same in "
          "every shot") {
    float planeMetres = 0.0f;
    for (const float fovX : {34.0f, 46.0f, 63.0f, 79.0f, 90.0f}) {
        CinemaStereoInput in;
        in.interaxialMetres = 0.5f;
        in.fovXDegrees = fovX;
        in.fovYDegrees = fovX * 0.6f;
        const auto s = cinemaStereo(in);
        REQUIRE(s.has_value());
        const float tanX = std::tan(fovX * 0.5f * 3.14159265f / 180.0f);
        CHECK(s->comfortMetres == doctest::Approx(2.0f * tanX * kCinemaNearestMetres *
                                                  (kCinemaInfinityShift + kCinemaMaxCrossed)));
        if (planeMetres == 0.0f) {
            planeMetres = s->zeroParallaxMetres;
        }
        CHECK(s->zeroParallaxMetres == doctest::Approx(planeMetres).epsilon(1e-3));
    }
    // 0.7 m * (0.01 + 0.0218) / 0.01.
    CHECK(planeMetres == doctest::Approx(2.226f).epsilon(1e-3));
}

TEST_CASE("the lens the traces cover is the band's, not the tall image's") {
    // A game view of 58.72 degrees drawn across a 2048x2208 image for a 16:9 screen: 90 x 94.31 degrees, of
    // which the screen shows the band of 58.72 degrees.
    CHECK(cinemaLensFovY(90.0f, 94.31f, 16.0 / 9.0) == doctest::Approx(58.72f).epsilon(1e-3));
    // The image is already as wide as the screen, or the whole image is shown: its own.
    CHECK(cinemaLensFovY(95.0f, 63.09f, 16.0 / 9.0) == doctest::Approx(63.09f).epsilon(1e-3));
    CHECK(cinemaLensFovY(90.0f, 94.31f, 0.0) == doctest::Approx(94.31f));
}

TEST_CASE("a traced near object lowers the separation to keep it within the crossed limit") {
    CinemaStereoInput in = wide(0.063f);
    in.fovXDegrees = 90.0f;
    in.fovYDegrees = 94.31f;
    const auto none = cinemaStereo(in);
    in.traceNearestMetres = 0.4f;
    const auto traced = cinemaStereo(in);
    REQUIRE(none.has_value());
    REQUIRE(traced.has_value());
    CHECK(traced->clamped);
    CHECK(traced->interaxialMetres < none->interaxialMetres);
    CHECK(traced->nearestMetres == doctest::Approx(0.4f));
    // The object at the traced distance sits at the crossed limit, measured through the frusta.
    CHECK(measuredDisparity(*traced, 0.4f, 0.0f) == doctest::Approx(-kCinemaMaxCrossed).epsilon(1e-2));
    // The control: what the separation without the trace would give at that distance is well past the limit.
    CHECK(measuredDisparity(*none, 0.4f, 0.0f) < -1.5f * kCinemaMaxCrossed);
}

TEST_CASE("a trace never loosens the assumed nearest distance, and is floored") {
    CinemaStereoInput in = wide(0.063f);
    in.fovXDegrees = 90.0f;
    in.fovYDegrees = 94.31f;
    const auto none = cinemaStereo(in);
    in.traceNearestMetres = 5.0f; // farther than the assumed 0.7 m
    const auto far = cinemaStereo(in);
    in.traceNearestMetres = 0.05f; // a camera in a wall: floored
    const auto wall = cinemaStereo(in);
    REQUIRE(none.has_value());
    REQUIRE(far.has_value());
    REQUIRE(wall.has_value());
    CHECK(far->comfortMetres == doctest::Approx(none->comfortMetres));
    CHECK(wall->nearestMetres == doctest::Approx(kCinemaMinTraceMetres));
    CHECK(wall->comfortMetres < none->comfortMetres);
}

TEST_CASE("a smoothed limit is used when given, and never above this frame's") {
    CinemaStereoInput in = wide(0.063f);
    in.traceNearestMetres = 0.5f;
    const auto frame = cinemaStereo(in);
    REQUIRE(frame.has_value());
    in.limitMetres = 0.5f * frame->comfortMetres;
    const auto lower = cinemaStereo(in);
    REQUIRE(lower.has_value());
    CHECK(lower->interaxialMetres == doctest::Approx(0.5f * frame->comfortMetres));
    CHECK(lower->comfortMetres == doctest::Approx(frame->comfortMetres)); // still reports this frame's
}

TEST_CASE("the ray fan covers the view in 5 columns by 3 rows") {
    const auto fan = cinemaRayFan(1.0f, 0.5625f);
    REQUIRE(fan.size() == 15);
    float minRight = 9.0f, maxRight = -9.0f, minUp = 9.0f, maxUp = -9.0f;
    bool centre = false;
    for (const CinemaRay& r : fan) {
        minRight = std::min(minRight, r.right);
        maxRight = std::max(maxRight, r.right);
        minUp = std::min(minUp, r.up);
        maxUp = std::max(maxUp, r.up);
        centre = centre || (r.right == 0.0f && r.up == 0.0f);
    }
    CHECK(minRight == doctest::Approx(-0.8f));
    CHECK(maxRight == doctest::Approx(0.8f));
    CHECK(minUp == doctest::Approx(-0.7f * 0.5625f));
    CHECK(maxUp == doctest::Approx(0.7f * 0.5625f));
    CHECK(centre);
}

TEST_CASE("a ray to the right of the picture points along the camera's -left") {
    // A camera turned in the world: forward +y, left -x, up +z (a right-handed id Tech axis).
    const IdViewAxis axis{{0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    const evr::Vec3 right = cinemaRayDirection(axis, {0.5f, 0.0f});
    CHECK(right.x > 0.0f); // the picture's right is world +x here
    CHECK(right.y > 0.0f);
    const evr::Vec3 up = cinemaRayDirection(axis, {0.0f, 0.5f});
    CHECK(up.z > 0.0f);
    CHECK(std::fabs(right.x * right.x + right.y * right.y + right.z * right.z - 1.0f) < 1e-5f);
    // It lands where its tangent says in an eye's image: the same sign as the point imageFraction places.
    const auto stereo = cinemaStereo(wide());
    REQUIRE(stereo.has_value());
    const float forward = right.y;       // along the camera's forward
    const float leftOfCamera = -right.x; // along the camera's left (axis.left = -x)
    CHECK(imageFraction(stereo->eyes[0], forward, leftOfCamera) > 0.5f); // right of the centre
}

TEST_CASE("the separation limit falls at once and rises slowly, and a cut resets it") {
    // Falls at once.
    CHECK(cinemaSmoothLimit(0.06f, 0.03f, 0.016f, false) == doctest::Approx(0.03f));
    // First value, or a camera cut: the target.
    CHECK(cinemaSmoothLimit(0.0f, 0.05f, 0.016f, false) == doctest::Approx(0.05f));
    CHECK(cinemaSmoothLimit(0.02f, 0.06f, 0.016f, true) == doctest::Approx(0.06f));
    // Rises toward the target by the time constant, never past it.
    float limit = 0.02f;
    for (int i = 0; i < 10; ++i) {
        limit = cinemaSmoothLimit(limit, 0.06f, 0.1f, false); // 1 s in 0.1 s steps
    }
    CHECK(limit > 0.02f);
    CHECK(limit < 0.06f);
    CHECK(limit == doctest::Approx(0.06f - 0.04f * std::exp(-1.0f / kCinemaLimitRiseSeconds)).epsilon(1e-3));
    for (int i = 0; i < 200; ++i) {
        limit = cinemaSmoothLimit(limit, 0.06f, 0.1f, false);
    }
    CHECK(limit <= 0.06f);
    CHECK(limit == doctest::Approx(0.06f).epsilon(1e-3));
    // A long gap is one step of at most 0.1 s; a bad target leaves the limit as it is.
    CHECK(cinemaSmoothLimit(0.02f, 0.06f, 5.0f, false) == cinemaSmoothLimit(0.02f, 0.06f, 0.1f, false));
    CHECK(cinemaSmoothLimit(0.02f, 0.0f, 0.016f, false) == doctest::Approx(0.02f));
}

TEST_CASE("bad input gives no eyes") {
    CHECK_FALSE(cinemaStereo({0.063f, 0.0f, 63.0f, 1.0f}).has_value());
    CHECK_FALSE(cinemaStereo({0.063f, 95.0f, 175.0f, 1.0f}).has_value());
    CHECK_FALSE(cinemaStereo({0.0f, 95.0f, 63.0f, 1.0f}).has_value());
    CHECK_FALSE(cinemaStereo({0.063f, 95.0f, 63.0f, 0.0f}).has_value());
    CHECK_FALSE(cinemaStereo({std::nanf(""), 95.0f, 63.0f, 1.0f}).has_value());
    CHECK_FALSE(cinemaStereo({0.063f, std::nanf(""), 63.0f, 1.0f}).has_value());
}
