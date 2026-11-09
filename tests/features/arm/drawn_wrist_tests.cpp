#include "features/arm/drawn_wrist.hpp"

#include "xr_math/head_aim.hpp"

#include <doctest/doctest.h>

#include <array>

using evr::Vec3;
using evr::arm::drawnWrist;
using evr::arm::FramePlacement;
using evr::arm::placedPoint;
using evr::arm::PosedWrist;
using evr::arm::ReadWrist;
using evr::xr_math::axisFromAngles;
using evr::xr_math::EyeRelativePose;

namespace {

constexpr float kUpm = 39.37f;
const Vec3 kEye{1200.0f, -340.0f, 64.0f};
// The arms model near the weapon hand: about 0.3 m ahead of the eye, 0.25 m right and 0.3 m below.
const EyeRelativePose kPlaced{Vec3{0.3f, -0.25f, -0.3f} * kUpm, axisFromAngles({5.0f, 20.0f, 0.0f})};
// The weapon hand turned 15 degrees and moved 2 cm.
const EyeRelativePose kMoved{kPlaced.offset + Vec3{0.0f, 0.0f, 0.02f} * kUpm,
                             axisFromAngles({5.0f, 35.0f, 0.0f})};
// The off hand's wrist in the model's space: ahead of the model and well to its left.
const Vec3 kWrist = Vec3{0.25f, 0.45f, 0.05f} * kUpm;
// Posed in frame 41 under kPlaced, from the hands of frame 41 (2.011 s) under a placement from them too.
const PosedWrist kPosed{41, kEye, kPlaced, kWrist, 2.011, 2.011};

bool near(Vec3 a, Vec3 b, float metres) {
    return length(a - b) <= metres;
}

} // namespace

TEST_CASE("a point in the arms model's space is placed by its axis rows from the eye") {
    const EyeRelativePose p{{1.0f, 2.0f, 3.0f}, axisFromAngles({0.0f, 90.0f, 0.0f})};
    // Turned 90 degrees left: the model's forward is the world's left (+y), its left the world's -x.
    const Vec3 w = placedPoint({10.0f, 0.0f, 0.0f}, p, {1.0f, 2.0f, 0.5f});
    CHECK(w.x == doctest::Approx(10.0f + 1.0f - 2.0f).epsilon(1e-5));
    CHECK(w.y == doctest::Approx(2.0f + 1.0f).epsilon(1e-5));
    CHECK(w.z == doctest::Approx(3.0f + 0.5f).epsilon(1e-5));
}

TEST_CASE("an off hand drawn in the next frame under the placement it was posed under lands where meant") {
    const std::array<FramePlacement, 1> placements{FramePlacement{42, kEye, kPlaced}};
    const auto d = drawnWrist(kPosed, placements, ReadWrist{42, kWrist}, kUpm);
    REQUIRE(d.has_value());
    CHECK(near(d->drawn, d->meant, 1e-5f));
    CHECK(d->snapshotGapMs == doctest::Approx(0.0));
    // Metres: the eye over the units per metre plus the placed wrist.
    CHECK(near(d->meant, placedPoint(kEye, kPlaced, kWrist) * (1.0f / kUpm), 1e-5f));
}

TEST_CASE("an off hand drawn under a placement the weapon hand moved since is off by that move") {
    // Posed from the hands of frame 41 under the placement made from the frame before's (0.011 s older).
    const PosedWrist posed{41, kEye, kPlaced, kWrist, 2.011, 2.000};
    const std::array<FramePlacement, 1> placements{FramePlacement{42, kEye, kMoved}};
    const auto d = drawnWrist(posed, placements, ReadWrist{42, kWrist}, kUpm);
    REQUIRE(d.has_value());
    const float off = length(d->drawn - d->meant);
    // The wrist sits about 0.5 m from the model's origin: 15 degrees swings it about 13 cm.
    const float expected =
        length(placedPoint(kEye, kMoved, kWrist) - placedPoint(kEye, kPlaced, kWrist)) / kUpm;
    CHECK(off == doctest::Approx(expected).epsilon(1e-4));
    CHECK(off > 0.10f);
    CHECK(d->snapshotGapMs == doctest::Approx(11.0).epsilon(1e-3));
}

TEST_CASE(
    "the drawn wrist is under the placement of the frame it is read in, not the frame it was written in") {
    // Written in 41 under kPlaced, the frame-41 placement; the weapon hand moved for frame 42, the frame that
    // draws it: the glove is drawn off although it was posed under its own frame's placement.
    const std::array<FramePlacement, 3> placements{FramePlacement{40, kEye, kPlaced},
                                                   FramePlacement{41, kEye, kPlaced},
                                                   FramePlacement{42, kEye, kMoved}};
    const auto d = drawnWrist(kPosed, placements, ReadWrist{42, kWrist}, kUpm);
    REQUIRE(d.has_value());
    CHECK(length(d->drawn - d->meant) > 0.10f);
    // Read in frame 42, whose placement is the one it was posed under: drawn where meant.
    const std::array<FramePlacement, 2> still{FramePlacement{41, kEye, kMoved},
                                              FramePlacement{42, kEye, kPlaced}};
    const auto s = drawnWrist(kPosed, still, ReadWrist{42, kWrist}, kUpm);
    REQUIRE(s.has_value());
    CHECK(near(s->drawn, s->meant, 1e-5f));
}

TEST_CASE("a read not of the next frame, a frame the model was not placed in, or no units measure nothing") {
    const std::array<FramePlacement, 2> placements{FramePlacement{41, kEye, kPlaced},
                                                   FramePlacement{42, kEye, kPlaced}};
    CHECK_FALSE(drawnWrist(kPosed, placements, ReadWrist{41, kWrist}, kUpm).has_value());
    CHECK_FALSE(drawnWrist(kPosed, placements, ReadWrist{43, kWrist}, kUpm).has_value());
    const std::array<FramePlacement, 1> other{FramePlacement{41, kEye, kPlaced}};
    CHECK_FALSE(drawnWrist(kPosed, other, ReadWrist{42, kWrist}, kUpm).has_value());
    CHECK_FALSE(drawnWrist(kPosed, placements, ReadWrist{42, kWrist}, 0.0f).has_value());
}

TEST_CASE("a write that does not land shows as the read's distance from it") {
    const std::array<FramePlacement, 1> placements{FramePlacement{42, kEye, kPlaced}};
    const Vec3 read = kWrist + Vec3{0.0f, 0.0f, 0.004f} * kUpm; // 4 mm up in the model
    const auto d = drawnWrist(kPosed, placements, ReadWrist{42, read}, kUpm);
    REQUIRE(d.has_value());
    CHECK(length(d->drawn - d->meant) == doctest::Approx(0.004f).epsilon(1e-3));
}
