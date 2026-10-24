#include "stereo_seq/decal_frustum.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

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

TEST_CASE(
    "the decals chosen for the pair are the active, inside ones the engine's list lacks, nearest first") {
    using evr::stereo_seq::nearestDecals;
    std::array<std::uint64_t, 2> visible{0x3ull, 0x8000000000000000ull}; // 0, 1 and 127 already listed
    const auto active = [](std::size_t i) {
        return i != 60;
    };
    const auto inside = [](std::size_t i) {
        return i >= 40 && i < 100;
    };
    const auto distance2 = [](std::size_t i) {
        const float d = static_cast<float>(i) - 64.0f;
        return d * d;
    };
    auto indices = nearestDecals(visible, 128, 1000, active, inside, distance2);
    // 40..99 inside, 60 inactive: 59 decals (in no particular order).
    REQUIRE(indices.size() == 59);
    CHECK(std::find(indices.begin(), indices.end(), 60) == indices.end()); // inactive
    CHECK(std::find(indices.begin(), indices.end(), 39) == indices.end()); // outside
    CHECK(std::find(indices.begin(), indices.end(), 100) == indices.end());
    // A decal the engine's list holds is not chosen again.
    visible[0] |= std::uint64_t{1} << 63;
    const auto without = nearestDecals(visible, 128, 1000, active, inside, distance2);
    CHECK(without.size() == 58);
    // Limited to 5 (63 is listed, 60 inactive): the nearest to 64 are 64, 65, 62 and 66 (a tie at 4),
    // then 61.
    auto nearest = nearestDecals(visible, 128, 5, active, inside, distance2);
    std::sort(nearest.begin(), nearest.end());
    CHECK(nearest == std::vector<std::uint16_t>{61, 62, 64, 65, 66});

    CHECK(std::find(without.begin(), without.end(), 63) == without.end());
}

TEST_CASE("only the nearest decals up to the limit are chosen") {
    using evr::stereo_seq::kPairCandidates;
    using evr::stereo_seq::nearestDecals;
    constexpr std::size_t kCount = 8192;
    const std::array<std::uint64_t, kCount / 64> visible{};
    const auto all = [](std::size_t) {
        return true;
    };
    // Distance grows with the index, so the nearest are the lowest indices.
    const auto distance2 = [](std::size_t i) {
        return static_cast<float>(i);
    };
    const auto indices = nearestDecals(visible, kCount, kPairCandidates, all, all, distance2);
    REQUIRE(indices.size() == kPairCandidates);
    for (std::size_t i = 0; i < indices.size(); ++i) {
        REQUIRE(indices[i] == i);
    }
    // Fewer candidates than the limit: all of them.
    CHECK(nearestDecals(visible, 100, kPairCandidates, all, all, distance2).size() == 100);
    CHECK(nearestDecals(visible, 100, 7, all, all, distance2).size() == 7);
}

TEST_CASE("the chosen decals only take the room the engine's own list leaves under the limit") {
    using evr::stereo_seq::extraRoom;
    using evr::stereo_seq::kViewDecalCapacity;
    using evr::stereo_seq::kViewDecalSafeCount;
    // On the rig the floor's decals held with 1,660 scored decals and were lost from 1,678.
    CHECK(kViewDecalCapacity >= 1660);
    CHECK(kViewDecalCapacity < 1678);
    CHECK(kViewDecalSafeCount < kViewDecalCapacity);
    CHECK(extraRoom(0) == kViewDecalSafeCount);
    CHECK(extraRoom(20) == kViewDecalSafeCount - 20);
    CHECK(extraRoom(kViewDecalSafeCount - 1) == 1);
    CHECK(extraRoom(kViewDecalSafeCount) == 0);
    CHECK(extraRoom(kViewDecalCapacity) == 0);
    CHECK(extraRoom(2048) == 0);
    // Below the limit the room is exactly what fills it.
    for (std::size_t kept : {0u, 20u, 1588u, 1599u, 1600u, 1631u}) {
        CHECK(kept + extraRoom(kept) == kViewDecalSafeCount);
    }
}

TEST_CASE("the chosen decals follow the engine's list, and it gives way only to make the minimum room") {
    using evr::stereo_seq::kPairMinimum;
    using evr::stereo_seq::kViewDecalSafeCount;
    using evr::stereo_seq::placeExtras;
    // A tunnel: the engine kept 14 decals; every chosen one that scored fits and nothing gives way.
    auto p = placeExtras(14, 1000);
    CHECK(p.keep == 14);
    CHECK(p.take == 1000);
    // Plenty of room, more scored than fits: the room is filled and nothing gives way.
    p = placeExtras(100, 5000);
    CHECK(p.keep == 100);
    CHECK(p.take == kViewDecalSafeCount - 100);
    // A busy scene: 1,382 kept leaves 250; the minimum takes 640 and the last 390 of the engine's give way.
    p = placeExtras(1382, 1969);
    CHECK(kViewDecalSafeCount - 1382 < kPairMinimum);
    CHECK(p.take == kPairMinimum);
    CHECK(p.keep == kViewDecalSafeCount - kPairMinimum);
    CHECK(1382 - p.keep == 1382 + kPairMinimum - kViewDecalSafeCount);
    // Fewer scored than the minimum: all of them, and the engine gives up only what they need.
    p = placeExtras(1600, 100);
    CHECK(p.take == 100);
    CHECK(p.keep == kViewDecalSafeCount - 100);
    // Nothing scored: the engine's list is left as it is, even past the limit.
    p = placeExtras(1700, 0);
    CHECK(p.keep == 1700);
    CHECK(p.take == 0);
    // The list never passes the limit once something is placed, and the engine's list is never reordered.
    for (std::size_t kept : {0u, 14u, 1382u, 1632u, 1700u, 2048u}) {
        for (std::size_t scored : {1u, 100u, 639u, 640u, 2048u}) {
            p = placeExtras(kept, scored);
            CHECK(p.keep <= kept);
            CHECK(p.take <= scored);
            CHECK(p.keep + p.take <= kViewDecalSafeCount);
        }
    }
}

TEST_CASE("the placement at the edges of the minimum room and of the limit") {
    using evr::stereo_seq::kPairMinimum;
    using evr::stereo_seq::kViewDecalSafeCount;
    using evr::stereo_seq::placeExtras;
    REQUIRE(kViewDecalSafeCount == 1632);
    REQUIRE(kPairMinimum == 640);
    // 992 kept leaves exactly the minimum: one fewer leaves one more, one more makes one give way.
    auto p = placeExtras(991, 2048);
    CHECK(p.keep == 991);
    CHECK(p.take == 641);
    p = placeExtras(992, 2048);
    CHECK(p.keep == 992);
    CHECK(p.take == 640);
    p = placeExtras(993, 2048);
    CHECK(p.keep == 992);
    CHECK(p.take == 640);
    // At and past the limit (room 1, 0, 0): the minimum is made by the engine's last-ranked decals.
    for (const std::size_t scored : {639u, 640u, 641u}) {
        const std::size_t take = std::min<std::size_t>(scored, 640);
        p = placeExtras(1631, scored);
        CHECK(p.take == take);
        CHECK(p.keep == 1632 - take);
        p = placeExtras(1632, scored);
        CHECK(p.take == take);
        CHECK(p.keep == 1632 - take);
        p = placeExtras(1633, scored);
        CHECK(p.take == take);
        CHECK(p.keep == 1632 - take);
    }
}

TEST_CASE("a record's index from its address, and the decal box distance") {
    using evr::stereo_seq::boxDistance2;
    using evr::stereo_seq::keyRecord;
    using evr::stereo_seq::kNoRecord;
    using evr::stereo_seq::recordIndex;
    std::array<std::byte, 0x50 * 4> records{};
    const std::byte* base = records.data();
    CHECK(recordIndex(base, base, 0x50, 4) == 0);
    CHECK(recordIndex(base + 0x50 * 3, base, 0x50, 4) == 3);
    CHECK(recordIndex(base + 0x50 * 4, base, 0x50, 4) == kNoRecord); // past the array
    CHECK(recordIndex(base + 0x28, base, 0x50, 4) == kNoRecord);     // not a record's start
    CHECK(recordIndex(base, base + 0x50, 0x50, 4) == kNoRecord);     // before the array
    CHECK(recordIndex(nullptr, base, 0x50, 4) == kNoRecord);
    // The record a key names: its low 16 bits, as the sorting job reads it.
    CHECK(keyRecord(0xABCD'1234'0000'7FFFull) == 0x7FFF);
    CHECK(keyRecord(0x0000'0001'0002'0003ull) == 3);
    // The distance to the box centre.
    CHECK(boxDistance2({0, 0, 0, 2, 4, 6}, {1, 2, 3}) == 0.0f);
    CHECK(boxDistance2({0, 0, 0, 2, 4, 6}, {4, 2, 3}) == 9.0f);
    CHECK(boxDistance2({-1, -1, -1, 1, 1, 1}, {1, 2, 2}) == 9.0f);
}

TEST_CASE("the chosen decals the engine's list already holds are left out") {
    using evr::stereo_seq::markHeld;
    using evr::stereo_seq::withoutHeld;
    std::array<std::byte, 0x50 * 200> records{};
    std::byte* base = records.data();
    // The engine's list: records 5, 64 and 199, and one that is not a record (left alone).
    std::array<std::byte*, 4> list{base + 5 * 0x50, base + 64 * 0x50, base + 199 * 0x50, base + 7};
    std::array<std::uint64_t, 4> held{~0ull, ~0ull, ~0ull, ~0ull}; // cleared first
    markHeld(held, list, base, 0x50);
    CHECK(held[0] == (std::uint64_t{1} << 5));
    CHECK(held[1] == 1);
    CHECK(held[2] == 0);
    CHECK(held[3] == (std::uint64_t{1} << (199 - 192)));
    const std::array<std::uint16_t, 6> chosen{3, 5, 64, 65, 199, 255};
    std::vector<std::uint16_t> out{42}; // cleared first
    withoutHeld(chosen, held, out);
    CHECK(out == std::vector<std::uint16_t>{3, 65, 255}); // 255: past the held bits, kept
}

TEST_CASE("the nearest scored keys take the places, ties by record") {
    using evr::stereo_seq::keepNearestKeys;
    // Keys with records 10..19 in a shuffled order and high bits set (the engine's score).
    std::vector<std::uint64_t> keys;
    for (const std::uint64_t r : {17u, 12u, 19u, 10u, 15u, 11u, 18u, 13u, 16u, 14u}) {
        keys.push_back((0xFFFFull << 48) | (r * 7) << 16 | r);
    }
    const auto byRecord = [](std::size_t record) {
        return static_cast<float>(record);
    };
    keepNearestKeys(keys, 4, byRecord);
    std::vector<std::size_t> first;
    for (std::size_t i = 0; i < 4; ++i) {
        first.push_back(keys[i] & 0xFFFF);
    }
    std::sort(first.begin(), first.end());
    CHECK(first == std::vector<std::size_t>{10, 11, 12, 13});
    CHECK(((keys[0] >> 48) & 0xFFFF) == 0xFFFF); // the key moves whole
    // Ties go to the lower record, whatever the order.
    std::vector<std::uint64_t> tied{9, 4, 7, 2, 8};
    keepNearestKeys(tied, 2, [](std::size_t) { return 1.0f; });
    std::sort(tied.begin(), tied.begin() + 2);
    CHECK(tied[0] == 2);
    CHECK(tied[1] == 4);
    // Room for all of them: nothing moves.
    std::vector<std::uint64_t> few{3, 1, 2};
    keepNearestKeys(few, 3, byRecord);
    CHECK(few == std::vector<std::uint64_t>{3, 1, 2});
}

TEST_CASE("the placed decals and their keys follow the kept part of the engine's list") {
    using evr::stereo_seq::Placement;
    using evr::stereo_seq::writeExtras;
    std::array<std::byte, 0x50 * 100> records{};
    std::byte* base = records.data();
    std::array<std::byte*, 8> list{};
    std::array<std::uint64_t, 8> keys{};
    for (std::size_t i = 0; i < 8; ++i) {
        list[i] = base + i * 0x50;
        keys[i] = 0x1000 + i;
    }
    const std::array<std::uint64_t, 3> extras{(5ull << 32) | 40, (6ull << 32) | 41, (7ull << 32) | 42};
    // Keep 4 of the engine's, place 2: list and keys from slot 4 on, the count 6.
    CHECK(writeExtras(list, keys, Placement{4, 2}, extras, base, 0x50) == 6);
    CHECK(list[3] == base + 3 * 0x50); // the kept part untouched
    CHECK(keys[3] == 0x1003);
    CHECK(list[4] == base + 40 * 0x50);
    CHECK(keys[4] == ((5ull << 32) | 40));
    CHECK(list[5] == base + 41 * 0x50);
    CHECK(keys[5] == ((6ull << 32) | 41));
    CHECK(list[6] == base + 6 * 0x50); // past the count: untouched
    // Never past the arrays: 7 kept leave one slot.
    CHECK(writeExtras(list, keys, Placement{7, 3}, extras, base, 0x50) == 8);
    CHECK(list[7] == base + 40 * 0x50);
    // More places than extras: only the extras.
    CHECK(writeExtras(list, keys, Placement{1, 5}, extras, base, 0x50) == 4);
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
