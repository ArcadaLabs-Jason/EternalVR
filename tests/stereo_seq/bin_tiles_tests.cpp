#include "stereo_seq/bin_tiles.hpp"

#include <doctest/doctest.h>

#include <cmath>

using evr::stereo_seq::binTileParams;
using evr::stereo_seq::BinTileParams;
using evr::stereo_seq::engineBinTileParams;
using evr::stereo_seq::Matrix4;

namespace {

// A projection in xr_math::engineProjection's layout for the frustum's tangents (left and down negative).
Matrix4 projection(float left, float right, float up, float down) {
    const float w = right - left;
    const float h = up - down;
    return {2.0f / w, 0, (right + left) / w, 0, 0, 2.0f / h, (up + down) / h, 0, 0, 0, 0, 1, 0, 0, -1, 0};
}

float deg(float d) {
    return std::tan(d * 0.017453292f);
}

bool near(const BinTileParams& a, const BinTileParams& b) {
    const auto close = [](float x, float y) {
        return std::fabs(x - y) < 1e-5f;
    };
    return close(a.width, b.width) && close(a.height, b.height) && close(a.left, b.left) &&
           close(a.top, b.top);
}

} // namespace

TEST_CASE("a symmetric projection gives the engine's own bin tile parameters") {
    const float tx = deg(54.0f);
    const float ty = deg(54.27f);
    const auto p = binTileParams(projection(-tx, tx, ty, -ty), 1904, 2048);
    REQUIRE(p);
    CHECK(near(*p, engineBinTileParams(108.0f, 108.54f, 1904, 2048)));
}

TEST_CASE("an eye's asymmetric frustum starts its tiles at its own edges and spans its own width") {
    // Quest 3's left eye: 54 degrees out, 40 in, 43.98 up, 54.27 down.
    const float l = -deg(54.0f), r = deg(40.0f), u = deg(43.98f), d = -deg(54.27f);
    const auto p = binTileParams(projection(l, r, u, d), 1904, 2048);
    REQUIRE(p);
    CHECK(p->left == doctest::Approx(l).epsilon(1e-5));
    CHECK(p->top == doctest::Approx(-u).epsilon(1e-5));
    CHECK(p->width == doctest::Approx((r - l) * 32.0f / 1904.0f).epsilon(1e-5));
    CHECK(p->height == doctest::Approx((u - d) * 32.0f / 2048.0f).epsilon(1e-5));
    // The last column's right edge is the frustum's right edge.
    CHECK(p->left + p->width * (1904.0f / 32.0f) == doctest::Approx(r).epsilon(1e-4));
    // The last row's bottom edge is the frustum's down edge (rows grow downwards).
    CHECK(p->top + p->height * (2048.0f / 32.0f) == doctest::Approx(-d).epsilon(1e-4));
}

TEST_CASE("the symmetric enclosing frustum the engine assumes misses the eye's edges") {
    const float l = -deg(54.0f), r = deg(40.0f);
    const auto eye = binTileParams(projection(l, r, deg(45.0f), -deg(45.0f)), 1904, 2048);
    REQUIRE(eye);
    const BinTileParams engine = engineBinTileParams(108.0f, 90.0f, 1904, 2048);
    CHECK(engine.left == doctest::Approx(eye->left).epsilon(1e-5)); // the wide side agrees
    CHECK(engine.width > eye->width * 1.2f);                        // the narrow side does not
}

TEST_CASE("bin tile parameters refuse non-perspective matrices and empty sizes") {
    const Matrix4 identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    CHECK(binTileParams(identity, 1904, 2048)); // scale 1 is a valid frustum of +-45 degrees
    Matrix4 zero{};
    CHECK_FALSE(binTileParams(zero, 1904, 2048));
    CHECK_FALSE(binTileParams(projection(-1, 1, 1, -1), 0, 2048));
    Matrix4 nan = projection(-1, 1, 1, -1);
    nan[2] = std::nanf("");
    CHECK_FALSE(binTileParams(nan, 1904, 2048));
}
