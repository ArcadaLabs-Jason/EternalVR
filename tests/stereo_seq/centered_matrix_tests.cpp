#include "stereo_seq/centered_matrix.hpp"

#include <doctest/doctest.h>

#include <cmath>

using evr::stereo_seq::CenteredDepth;
using evr::stereo_seq::centeredDepthOf;
using evr::stereo_seq::Matrix4;
using evr::stereo_seq::retargetViewProjection;
using evr::stereo_seq::setCenteredDepth;

namespace {

Matrix4 multiply(const Matrix4& a, const Matrix4& b) {
    Matrix4 r{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            for (int k = 0; k < 4; ++k) {
                r[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
            }
        }
    }
    return r;
}

Matrix4 projection(float sx, float ox, float sy, float oy, float a, float b) {
    return {sx, 0, ox, 0, 0, sy, oy, 0, 0, 0, a, b, 0, 0, -1, 0};
}

// A rotation-only view: 30 degrees yaw, then 10 degrees pitch.
Matrix4 view() {
    const float y = 0.5235988f;
    const float p = 0.1745329f;
    const Matrix4 yaw{std::cos(y),  0, std::sin(y), 0, 0, 1, 0, 0,
                      -std::sin(y), 0, std::cos(y), 0, 0, 0, 0, 1};
    const Matrix4 pitch{1, 0, 0, 0, 0, std::cos(p), -std::sin(p), 0, 0, std::sin(p), std::cos(p),
                        0, 0, 0, 0, 1};
    return multiply(pitch, yaw);
}

} // namespace

TEST_CASE("centred matrix: the depth row comes back out of a product") {
    const Matrix4 c = multiply(projection(0.72f, 0.0f, 0.71f, 0.0f, -1.00001f, -0.5f), view());
    const auto d = centeredDepthOf(c);
    REQUIRE(d.has_value());
    CHECK(d->a == doctest::Approx(-1.00001f));
    CHECK(d->b == doctest::Approx(-0.5f));
}

TEST_CASE("centred matrix: an explicit eye projection gets the engine's depth row back") {
    const Matrix4 v = view();
    const Matrix4 engine = multiply(projection(0.72f, 0.0f, 0.71f, 0.0f, -1.0f, -0.25f), v);
    const Matrix4 eye = projection(0.9f, -0.24f, 0.85f, -0.18f, -1.000001f, -0.06f);
    const Matrix4 wanted = multiply(projection(0.9f, -0.24f, 0.85f, -0.18f, -1.0f, -0.25f), v);
    Matrix4 latched = multiply(eye, v);
    const auto d = centeredDepthOf(engine);
    REQUIRE(d.has_value());
    setCenteredDepth(latched, *d);
    for (int i = 0; i < 16; ++i) {
        CHECK(latched[i] == doctest::Approx(wanted[i]).epsilon(1e-5));
    }
}

TEST_CASE("centred matrix: not a perspective product") {
    Matrix4 ortho{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    CHECK_FALSE(centeredDepthOf(ortho).has_value());
    Matrix4 bad = multiply(projection(1, 0, 1, 0, -1, -0.1f), view());
    bad[9] += 0.5f; // row 2 no longer a multiple of row 3
    CHECK_FALSE(centeredDepthOf(bad).has_value());
}

TEST_CASE("centred matrix: hands-and-guns matrices take the eye's frustum") {
    Matrix4 v = view();
    v[3] = 12.0f; // with a translation, as customViewProjectionMatrix has
    v[7] = -3.0f;
    v[11] = 40.0f;
    const Matrix4 weapon = projection(1.94f, 0.0f, 2.3f, 0.0f, -1.0f, -0.2f);
    const Matrix4 eye = projection(0.9f, -0.24f, 0.85f, -0.18f, -1.000001f, -0.06f);
    Matrix4 m = multiply(weapon, v);
    REQUIRE(retargetViewProjection(m, eye));
    const Matrix4 wantEyeRows = multiply(eye, v);
    const Matrix4 keep = multiply(weapon, v);
    for (int i = 0; i < 8; ++i) {
        CHECK(m[i] == doctest::Approx(wantEyeRows[i]).epsilon(1e-5));
    }
    for (int i = 8; i < 16; ++i) {
        CHECK(m[i] == keep[i]); // the weapon's depth rows stay
    }
    Matrix4 zero{};
    CHECK_FALSE(retargetViewProjection(zero, eye));
}
