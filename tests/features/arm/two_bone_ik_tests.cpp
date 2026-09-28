#include "features/arm/two_bone_ik.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <limits>

using evr::Vec3;
using evr::arm::kMaxReachFraction;
using evr::arm::solveTwoBone;
using evr::arm::TwoBoneInput;
using evr::test::approxEqual;

namespace {

// The bones keep their lengths whatever the target.
void checkLengths(const evr::arm::TwoBoneSolution& s, const TwoBoneInput& in) {
    CHECK(approxEqual(length(s.joint - in.root), in.upper));
    CHECK(approxEqual(length(s.end - s.joint), in.lower));
}

} // namespace

TEST_CASE("a target within reach is reached, bending toward the pole") {
    const TwoBoneInput in{{0.0f, 0.0f, 0.0f}, {0.4f, 0.0f, 0.0f}, 0.3f, 0.3f, {0.0f, 0.0f, -1.0f}};
    const auto s = solveTwoBone(in);
    REQUIRE(s);
    CHECK(approxEqual(s->end, in.target));
    CHECK_FALSE(s->clamped);
    CHECK(approxEqual(s->reach, 0.4f / 0.6f));
    checkLengths(*s, in);
    // Symmetric bones: the elbow halfway along, below the line.
    CHECK(approxEqual(s->joint.x, 0.2f));
    CHECK(approxEqual(s->joint.y, 0.0f));
    CHECK(s->joint.z < -0.2f);
    CHECK(approxEqual(s->bend, Vec3{0.0f, 0.0f, -1.0f}));
}

TEST_CASE("a target beyond reach stops the wrist short on the line to it") {
    const TwoBoneInput in{{0.1f, 0.2f, 0.3f}, {2.1f, 0.2f, 0.3f}, 0.28f, 0.28f, {0.0f, 1.0f, -1.0f}};
    const auto s = solveTwoBone(in);
    REQUIRE(s);
    CHECK(s->clamped);
    CHECK(s->reach > 3.0f);
    CHECK(approxEqual(s->end, in.root + Vec3{0.56f * kMaxReachFraction, 0.0f, 0.0f}));
    checkLengths(*s, in);
    // Nearly straight, still bending toward the pole.
    CHECK(s->joint.y > in.root.y);
    CHECK(s->joint.z < in.root.z);
}

TEST_CASE("a target at the full length is clamped just short of it") {
    const TwoBoneInput in{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.5f}, 0.25f, 0.25f, {1.0f, 0.0f, 0.0f}};
    const auto s = solveTwoBone(in);
    REQUIRE(s);
    CHECK(s->clamped);
    CHECK(approxEqual(length(s->end - in.root), 0.5f * kMaxReachFraction));
    checkLengths(*s, in);
}

TEST_CASE("a target too close is pushed out to the shortest reach") {
    const TwoBoneInput in{{0.0f, 0.0f, 0.0f}, {0.01f, 0.0f, 0.0f}, 0.3f, 0.2f, {0.0f, 0.0f, -1.0f}};
    const auto s = solveTwoBone(in);
    REQUIRE(s);
    CHECK(s->clamped);
    CHECK(approxEqual(s->end, Vec3{0.1f, 0.0f, 0.0f}));
    checkLengths(*s, in);
}

TEST_CASE("the elbow goes to the pole's side of the shoulder-wrist line") {
    for (const Vec3 pole : {Vec3{0.0f, 1.0f, 0.0f}, Vec3{0.0f, -1.0f, 0.0f}, Vec3{0.0f, 0.3f, -2.0f}}) {
        const TwoBoneInput in{{0.0f, 0.0f, 0.0f}, {0.3f, 0.1f, -0.1f}, 0.3f, 0.28f, pole};
        const auto s = solveTwoBone(in);
        REQUIRE(s);
        const Vec3 dir = normalize(in.target);
        const Vec3 off = s->joint - dir * dot(s->joint, dir);
        const Vec3 poleAcross = pole - dir * dot(pole, dir);
        CHECK(dot(off, poleAcross) > 0.0f);
        checkLengths(*s, in);
    }
}

TEST_CASE("a pole along the shoulder-wrist line still gives an elbow") {
    const TwoBoneInput in{{0.0f, 0.0f, 0.0f}, {0.4f, 0.0f, 0.0f}, 0.3f, 0.3f, {1.0f, 0.0f, 0.0f}};
    const auto s = solveTwoBone(in);
    REQUIRE(s);
    CHECK(approxEqual(s->end, in.target));
    CHECK(approxEqual(dot(s->bend, Vec3{1.0f, 0.0f, 0.0f}), 0.0f));
    checkLengths(*s, in);
}

TEST_CASE("zero lengths and non-finite inputs are refused") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(solveTwoBone({{}, {0.3f, 0.0f, 0.0f}, 0.0f, 0.3f, {0.0f, 0.0f, -1.0f}}));
    CHECK_FALSE(solveTwoBone({{}, {0.3f, 0.0f, 0.0f}, 0.3f, -0.1f, {0.0f, 0.0f, -1.0f}}));
    CHECK_FALSE(solveTwoBone({{}, {nan, 0.0f, 0.0f}, 0.3f, 0.3f, {0.0f, 0.0f, -1.0f}}));
    CHECK_FALSE(solveTwoBone({{}, {0.3f, 0.0f, 0.0f}, 0.3f, nan, {0.0f, 0.0f, -1.0f}}));
}

TEST_CASE("a target on the shoulder or no pole at all still gives a finite arm") {
    const TwoBoneInput onShoulder{{0.1f, 0.1f, 0.1f}, {0.1f, 0.1f, 0.1f}, 0.3f, 0.28f, {0.0f, 0.0f, -1.0f}};
    const auto a = solveTwoBone(onShoulder);
    REQUIRE(a);
    CHECK(a->clamped);
    checkLengths(*a, onShoulder);
    const TwoBoneInput noPole{{0.0f, 0.0f, 0.0f}, {0.3f, 0.2f, 0.0f}, 0.3f, 0.28f, {}};
    const auto b = solveTwoBone(noPole);
    REQUIRE(b);
    CHECK(approxEqual(b->end, noPole.target));
    checkLengths(*b, noPole);
}
