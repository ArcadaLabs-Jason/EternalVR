#include "features/roomscale/head_offset.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <ostream>

using evr::Vec3;
using namespace evr::roomscale;

namespace {

HeadOffset offsetFor(Vec3 head,
                     HeightMode mode = HeightMode::Slayer,
                     std::optional<float> floor = {},
                     float unitsPerMetre = 1.0f) {
    HeadOffsetInput in;
    in.roomHead = head;
    in.height = mode;
    in.anchorAboveFloor = floor;
    in.unitsPerMetre = unitsPerMetre;
    return headOffset(in, {});
}

} // namespace

TEST_CASE("a lean inside the cap passes unchanged") {
    const HeadOffset o = offsetFor({0.2f, -0.05f, -0.3f});
    CHECK_FALSE(o.leanClamped);
    CHECK(o.offset.x == doctest::Approx(0.2f));
    CHECK(o.offset.y == doctest::Approx(-0.05f));
    CHECK(o.offset.z == doctest::Approx(-0.3f));
}

TEST_CASE("a 0.8 m lean is clamped to 0.60 m in the same direction") {
    const HeadOffset o = offsetFor({0.8f * 0.6f, 0.0f, -0.8f * 0.8f});
    CHECK(o.leanClamped);
    CHECK(o.requestedLean == doctest::Approx(0.8f));
    CHECK(o.lean == doctest::Approx(0.60f));
    CHECK(std::fabs(o.lean - 0.60f) <= 0.01f);
    CHECK(std::hypot(o.offset.x, o.offset.z) == doctest::Approx(0.60f));
    CHECK(o.offset.x / o.offset.z == doctest::Approx(0.6f / -0.8f));
}

TEST_CASE("the cap is configurable") {
    HeadOffsetInput in;
    in.roomHead = {1.0f, 0.0f, 0.0f};
    HeadOffsetLimits limits;
    limits.leanCapMetres = 0.3f;
    CHECK(headOffset(in, limits).offset.x == doctest::Approx(0.3f));
    limits.leanCapMetres = std::nanf("");
    CHECK(headOffset(in, limits).offset.x == doctest::Approx(0.6f));
}

TEST_CASE("Slayer height: the anchored head is the game's eye, crouching and rising are clamped") {
    CHECK(offsetFor({0.0f, 0.0f, 0.0f}).offset.y == doctest::Approx(0.0f));
    // Rising more than 0.25 m above the anchor (a seated player standing up) is held at 0.25 m.
    const HeadOffset up = offsetFor({0.0f, 0.5f, 0.0f});
    CHECK(up.heightClamped);
    CHECK(up.offset.y == doctest::Approx(0.25f));
    // The eye never goes below 0.3 m above the feet: 1.657 - 0.3 = 1.357 m down at most.
    const HeadOffset down = offsetFor({0.0f, -1.6f, 0.0f});
    CHECK(down.heightClamped);
    CHECK(down.offset.y == doctest::Approx(0.3f - 1.657f));
}

TEST_CASE("Real height uses the floor, scaled by the world scale") {
    // A 1.80 m eye above the floor at 1.0 world scale: 0.143 m above the Slayer's.
    const HeadOffset real = offsetFor({0.0f, 0.0f, 0.0f}, HeightMode::Real, 1.80f);
    CHECK(real.realHeight);
    CHECK(real.offset.y == doctest::Approx(1.80f - 1.657f));
    // At 1.2 units per metre the eye lands at 1.80 m of the player's height, in game units.
    const HeadOffset scaled = offsetFor({0.0f, 0.0f, 0.0f}, HeightMode::Real, 1.80f, 1.2f);
    CHECK(scaled.offset.y * 1.2f + 1.657f == doctest::Approx(1.80f * 1.2f));
    // No floor: Slayer height.
    const HeadOffset noFloor = offsetFor({0.0f, 0.1f, 0.0f}, HeightMode::Real);
    CHECK_FALSE(noFloor.realHeight);
    CHECK(noFloor.offset.y == doctest::Approx(0.1f));
}

TEST_CASE("a non-finite head gives no offset") {
    const HeadOffset o = offsetFor({std::nanf(""), 0.0f, 0.0f});
    CHECK(o.offset.x == 0.0f);
    CHECK(o.offset.y == 0.0f);
    CHECK(o.offset.z == 0.0f);
}

TEST_CASE("the lean is horizontal only: standing up is not a lean") {
    // Rising 0.2 m (within the 0.25 m allowance) with a 0.3 m lean: the lean is 0.3 m, nothing clamps.
    const HeadOffset within = offsetFor({0.3f, 0.2f, 0.0f});
    CHECK(within.requestedLean == doctest::Approx(0.3f));
    CHECK_FALSE(within.leanClamped);
    CHECK_FALSE(within.heightClamped);
    CHECK(within.offset.y == doctest::Approx(0.2f));
    // A seated player standing straight up 0.7 m, 0.3 m forward and 0.3 m right of the chair: the lean
    // is the 0.42 m across the floor, still inside the cap; the rise is held at the allowance (0.25 m)
    // until the posture re-detection or a recenter re-anchors the height.
    const HeadOffset stood = offsetFor({0.3f, 0.7f, -0.3f});
    CHECK(stood.requestedLean == doctest::Approx(std::hypot(0.3f, 0.3f)));
    CHECK_FALSE(stood.leanClamped);
    CHECK(stood.heightClamped);
    CHECK(stood.offset.y == doctest::Approx(0.25f));
    // The same rise straight up: no lean at all.
    const HeadOffset up = offsetFor({0.0f, 0.7f, 0.0f});
    CHECK(up.requestedLean == doctest::Approx(0.0f));
    CHECK_FALSE(up.leanClamped);
}
