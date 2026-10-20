#include "stereo_seq/decal_frustum.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

using evr::stereo_seq::boxOutside;
using evr::stereo_seq::Matrix4;
using evr::stereo_seq::pairDecalProjection;
using evr::stereo_seq::SideFrustum;
using evr::stereo_seq::sideFrustum;

namespace {

// A projection in xr_math::engineProjection's layout for the frustum's tangents (left and down negative),
// with the depth rows of the engine's reversed infinite projection (near plane 0.06).
Matrix4 projection(float left, float right, float up, float down) {
    const float w = right - left;
    const float h = up - down;
    return {
        2.0f / w, 0, (right + left) / w, 0, 0, 2.0f / h, (up + down) / h, 0, 0, 0, -1.000007f, -0.06f, 0, 0,
        -1,       0};
}

float deg(float d) {
    return std::tan(d * 0.017453292f);
}

// The frustum's edge tangents (left, right, up, down) for an engineProjection matrix: where x_ndc and y_ndc
// reach -1 and +1.
struct Edges {
    float left, right, up, down;
};
Edges edges(const Matrix4& m) {
    return {(m[2] - 1.0f) / m[0], (m[2] + 1.0f) / m[0], (m[6] + 1.0f) / m[5], (m[6] - 1.0f) / m[5]};
}

} // namespace

TEST_CASE("a mono view at the flat game's own FOV comes back unchanged, bit for bit") {
    const Matrix4 m = projection(-deg(54.0f), deg(54.0f), deg(54.27f), -deg(54.27f));
    CHECK(pairDecalProjection(m, 108.0f, 108.54f) == m);
}

TEST_CASE("a Quest 3 eye gets the flat game's 108 x 108.54 degrees, centred on its own axis") {
    // A rig capture's left eye (Quest 3 profile): 54 degrees out, 40 in, 43.98 up, 54.27 down.
    const Matrix4 eye = projection(-deg(54.0f), deg(40.0f), deg(43.98f), -deg(54.27f));
    const Matrix4 culled = pairDecalProjection(eye, 108.0f, 108.54f);
    // Expected independently: 1 / tan(54 deg) = 0.726543, 1 / tan(54.27 deg) = 0.719367.
    CHECK(culled[0] == doctest::Approx(0.726543).epsilon(1e-4));
    CHECK(culled[5] == doctest::Approx(0.719367).epsilon(1e-4));
    CHECK(culled[2] == 0.0f);
    CHECK(culled[6] == 0.0f);
    // Depth rows (and so the near plane Umbra sees) are the eye's own.
    for (std::size_t i = 8; i < 16; ++i) {
        CHECK(culled[i] == eye[i]);
    }
    // The right eye is the mirror image: the same frustum.
    const Matrix4 right =
        pairDecalProjection(projection(-deg(40.0f), deg(54.0f), deg(43.98f), -deg(54.27f)), 108.0f, 108.54f);
    CHECK(right[0] == culled[0]);
    CHECK(right[5] == culled[5]);
}

TEST_CASE("every edge is at least as wide as the eye's own and at least as wide as the flat game's") {
    struct Case {
        const char* name;
        float left, right, up, down; // degrees, left and down negative
        float fovX, fovY;            // the flat game's view
    };
    const Case cases[] = {
        {"a Pimax-class 120-degree eye", -62.0f, 58.0f, 55.0f, -60.0f, 124.0f, 120.0f},
        {"a 70 / 30 split", -70.0f, 30.0f, 45.0f, -45.0f, 140.0f, 90.0f},
        {"vertical asymmetry upwards only", -45.0f, 45.0f, 50.0f, -30.0f, 90.0f, 100.0f},
        {"a narrow symmetric eye under a wider flat view", -35.0f, 35.0f, 35.0f, -35.0f, 100.0f, 100.0f},
        {"a narrow headset (45 out / 35 in)", -45.0f, 35.0f, 40.0f, -45.0f, 90.0f, 90.0f},
        {"a narrow headset under a wider flat view", -45.0f, 35.0f, 40.0f, -45.0f, 100.0f, 104.0f},
        {"no flat FOV known", -54.0f, 40.0f, 43.98f, -54.27f, 0.0f, std::numeric_limits<float>::quiet_NaN()},
    };
    for (const Case& c : cases) {
        CAPTURE(std::string(c.name));
        const float l = deg(c.left), r = deg(c.right), u = deg(c.up), d = deg(c.down);
        const Edges e = edges(pairDecalProjection(projection(l, r, u, d), c.fovX, c.fovY));
        const float slack = 1e-4f;
        // Centred.
        CHECK(e.left == doctest::Approx(-e.right).epsilon(1e-5));
        CHECK(e.up == doctest::Approx(-e.down).epsilon(1e-5));
        // At least the eye's own extent on every side.
        CHECK(e.left <= l + slack);
        CHECK(e.right >= r - slack);
        CHECK(e.up >= u - slack);
        CHECK(e.down <= d + slack);
        // At least the flat game's extent, when it is known.
        if (c.fovX > 0.0f) {
            CHECK(e.right >= deg(c.fovX / 2.0f) - slack);
        }
        if (c.fovY > 0.0f) {
            CHECK(e.up >= deg(c.fovY / 2.0f) - slack);
        }
    }
}

TEST_CASE("a matrix that is not a perspective projection comes back unchanged") {
    const Matrix4 base = projection(-deg(54.0f), deg(40.0f), deg(43.98f), -deg(54.27f));
    Matrix4 zero{};
    CHECK(pairDecalProjection(zero, 108.0f, 108.54f) == zero);
    for (const float bad : {0.0f, 1e-5f, -1.0f, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
        CAPTURE(bad);
        Matrix4 m = base;
        m[0] = bad;
        const Matrix4 u = pairDecalProjection(m, 108.0f, 108.54f);
        CHECK(u[2] == m[2]);
        CHECK(u[5] == m[5]);
        Matrix4 n = base;
        n[5] = bad;
        const Matrix4 v = pairDecalProjection(n, 108.0f, 108.54f);
        CHECK(v[0] == n[0]);
        CHECK(v[6] == n[6]);
    }
}

namespace {

// A Quest 3 left eye (rig capture profile): 54 degrees out (left), 40 in (right), 43.98 up, 54.27 down.
const Matrix4 kLeftEye = projection(-deg(54.0f), deg(40.0f), deg(43.98f), -deg(54.27f));

// The view axis rows forward, left, up for a yaw of `yawDegrees` about +z (yaw 0: forward +x, left +y).
std::array<float, 9> yawAxis(float yawDegrees) {
    const float r = yawDegrees * 0.017453292f;
    const float c = std::cos(r);
    const float s = std::sin(r);
    return {c, s, 0, -s, c, 0, 0, 0, 1};
}

// A point at `distance` along the view direction with tangents (right, up), in world space.
std::array<float, 3> along(const std::array<float, 3>& origin,
                           const std::array<float, 9>& axis,
                           float right,
                           float up,
                           float distance) {
    std::array<float, 3> p{};
    for (int i = 0; i < 3; ++i) {
        p[i] = origin[i] + distance * (axis[i] - right * axis[3 + i] + up * axis[6 + i]);
    }
    return p;
}

// A cube of half-size `h` around `c`.
bool cubeOutside(const SideFrustum& f, const std::array<float, 3>& c, float h) {
    const float mins[3] = {c[0] - h, c[1] - h, c[2] - h};
    const float maxs[3] = {c[0] + h, c[1] + h, c[2] + h};
    return boxOutside(f, mins, maxs);
}

} // namespace

TEST_CASE("a Quest 3 left eye's own side planes keep what it sees and not what lies past its inner side") {
    for (const float yaw : {0.0f, 90.0f, -137.0f}) {
        CAPTURE(yaw);
        const std::array<float, 3> o{381.0f, -886.0f, -130.3f};
        const auto axis = yawAxis(yaw);
        const auto f = sideFrustum(o, axis, kLeftEye);
        REQUIRE(f.has_value());
        // Straight ahead, 10 m away.
        CHECK_FALSE(cubeOutside(*f, along(o, axis, 0.0f, 0.0f, 10.0f), 0.1f));
        // 45 degrees to the right: past the inner 40-degree side, inside the flat game's 54.
        CHECK(cubeOutside(*f, along(o, axis, deg(45.0f), 0.0f, 10.0f), 0.1f));
        // 50 degrees to the left: inside the outer 54-degree side.
        CHECK_FALSE(cubeOutside(*f, along(o, axis, -deg(50.0f), 0.0f, 10.0f), 0.1f));
        // 56 degrees to the left: past the outer side.
        CHECK(cubeOutside(*f, along(o, axis, -deg(56.0f), 0.0f, 10.0f), 0.1f));
        // Centred at 45 degrees right but large enough to reach across the 40-degree edge: kept.
        CHECK_FALSE(cubeOutside(*f, along(o, axis, deg(45.0f), 0.0f, 10.0f), 1.5f));
        // Up past 43.98 degrees and down within 54.27.
        CHECK(cubeOutside(*f, along(o, axis, 0.0f, deg(47.0f), 10.0f), 0.1f));
        CHECK_FALSE(cubeOutside(*f, along(o, axis, 0.0f, -deg(52.0f), 10.0f), 0.1f));
        // Behind the eye.
        CHECK(cubeOutside(*f, along(o, axis, 0.0f, 0.0f, -10.0f), 0.1f));
        // A box around the eye itself.
        CHECK_FALSE(cubeOutside(*f, o, 0.5f));
    }
}

TEST_CASE("a point is inside the side planes exactly when the projection puts it inside the picture") {
    const std::array<float, 3> o{12.0f, -3.0f, 4.0f};
    const auto axis = yawAxis(33.0f);
    const Matrix4 eyes[] = {kLeftEye, projection(-deg(40.0f), deg(54.0f), deg(43.98f), -deg(54.27f)),
                            projection(-deg(30.0f), deg(70.0f), deg(20.0f), -deg(60.0f))};
    for (const Matrix4& m : eyes) {
        const auto f = sideFrustum(o, axis, m);
        REQUIRE(f.has_value());
        int checked = 0;
        for (float tx = -2.0f; tx <= 2.0f; tx += 0.0625f) {
            for (float ty = -2.0f; ty <= 2.0f; ty += 0.0625f) {
                // x_ndc = m0 * tan - m2, y_ndc = m5 * tan - m6 (the layout's convention), computed apart.
                const float xNdc = m[0] * tx - m[2];
                const float yNdc = m[5] * ty - m[6];
                if (std::fabs(std::fabs(xNdc) - 1.0f) < 1e-3f || std::fabs(std::fabs(yNdc) - 1.0f) < 1e-3f) {
                    continue; // on an edge
                }
                const bool inside = std::fabs(xNdc) < 1.0f && std::fabs(yNdc) < 1.0f;
                const auto p = along(o, axis, tx, ty, 7.0f);
                CHECK(cubeOutside(*f, p, 0.0f) == !inside);
                ++checked;
            }
        }
        CHECK(checked > 3000);
    }
}

TEST_CASE("a decal past a Quest 3 eye's inner side but in the flat game's view is kept for the pair") {
    const std::array<float, 3> o{381.0f, -886.0f, -130.3f};
    const auto axis = yawAxis(140.0f);
    const auto own = sideFrustum(o, axis, kLeftEye);
    const auto pair = sideFrustum(o, axis, pairDecalProjection(kLeftEye, 108.0f, 108.54f));
    REQUIRE(own.has_value());
    REQUIRE(pair.has_value());
    // A floor patch 45 degrees to the right and below: outside eye L's 40-degree inner side, inside 54.
    const auto patch = along(o, axis, deg(45.0f), -deg(30.0f), 6.0f);
    CHECK(cubeOutside(*own, patch, 0.2f));
    CHECK_FALSE(cubeOutside(*pair, patch, 0.2f));
    // Past the flat game's 54 degrees on either side: not kept.
    CHECK(cubeOutside(*pair, along(o, axis, deg(57.0f), 0.0f, 6.0f), 0.2f));
    CHECK(cubeOutside(*pair, along(o, axis, -deg(57.0f), 0.0f, 6.0f), 0.2f));
}

TEST_CASE("decals are added only where active and inside, and every visible decal stays") {
    using evr::stereo_seq::addDecals;
    std::array<std::uint64_t, 2> visible{0x3ull, 0x8000000000000000ull}; // 0, 1 and 127 already visible
    std::size_t activeAsked = 0;
    const auto active = [&](std::size_t i) {
        ++activeAsked;
        return i != 5;
    };
    const auto inside = [](std::size_t i) {
        return i % 2 == 1 || i == 64;
    };
    // 100 records: odd ones and 64 inside; 5 inactive; 1 already visible.
    const std::size_t added = addDecals(visible, 100, active, inside);
    CHECK(added == 49); // odd 3..99 (49) minus 5, plus 64
    CHECK(visible[0] == (0xAAAAAAAAAAAAAAAAull & ~(std::uint64_t{1} << 5)) + 1);
    CHECK(visible[1] == (0x8000000000000000ull | 0x1ull | (0xAAAAAAAAAull & ((std::uint64_t{1} << 36) - 1))));
    CHECK(activeAsked == 98); // every record below the count but the visible 0 and 1
    // Nothing more to add.
    CHECK(addDecals(visible, 100, active, inside) == 0);
    // The count is clipped to the set's size.
    std::array<std::uint64_t, 1> small{0};
    CHECK(addDecals(small, 1000, active, inside) == 31);
}

TEST_CASE("what is not a view or not a box adds nothing") {
    const std::array<float, 3> o{0.0f, 0.0f, 0.0f};
    const auto axis = yawAxis(0.0f);
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    constexpr float inf = std::numeric_limits<float>::infinity();
    for (const float bad : {0.0f, 1e-5f, -1.0f, inf, nan}) {
        CAPTURE(bad);
        Matrix4 m = kLeftEye;
        m[0] = bad;
        CHECK_FALSE(sideFrustum(o, axis, m).has_value());
        m = kLeftEye;
        m[5] = bad;
        CHECK_FALSE(sideFrustum(o, axis, m).has_value());
    }
    CHECK_FALSE(sideFrustum({nan, 0.0f, 0.0f}, axis, kLeftEye).has_value());
    auto badAxis = axis;
    badAxis[4] = inf;
    CHECK_FALSE(sideFrustum(o, badAxis, kLeftEye).has_value());
    const auto f = sideFrustum(o, axis, kLeftEye);
    REQUIRE(f.has_value());
    const float mins[3] = {nan, -1.0f, -1.0f};
    const float maxs[3] = {nan, 1.0f, 1.0f};
    CHECK(boxOutside(*f, mins, maxs));
}
