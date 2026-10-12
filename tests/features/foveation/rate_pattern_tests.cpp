#include "features/foveation/rate_pattern.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using evr::Quat;
using evr::foveation::eyeTestPattern;
using evr::foveation::foveatedPattern;
using evr::foveation::foveationRegion;
using evr::foveation::FoveationRegion;
using evr::foveation::kRateFull;
using evr::foveation::kRateHalf;
using evr::foveation::kRateQuarter;
using evr::foveation::RatePatternSize;
using evr::foveation::rateShares;
using evr::xr_math::Fov;

namespace {

constexpr float kQuarterPi = std::numbers::pi_v<float> / 4.0f;

float degreesOfTangent(float tangent) {
    return std::atan(tangent) * 180.0f / std::numbers::pi_v<float>;
}

// A square 90 degree FOV: its tangents are +-1, so NDC and tangent units coincide and a cone around
// head-forward is a circle of radius tan(angle) around the image centre.
FoveationRegion squareRegion(float tangentRadius) {
    const Fov square{-kQuarterPi, kQuarterPi, kQuarterPi, -kQuarterPi};
    const auto r = foveationRegion(square, Quat::identity(), degreesOfTangent(tangentRadius));
    REQUIRE(r.has_value());
    return *r;
}

} // namespace

TEST_CASE("the pattern has one texel per block, rounded up") {
    const RatePatternSize size{100, 50, 16, 16};
    CHECK(foveatedPattern(size, {}, {}).size() == 7u * 4u);
    CHECK(foveatedPattern({0, 0, 16, 16}, {}, {}).empty());
}

TEST_CASE("the centre is full rate, a ring half rate, the corners quarter rate") {
    const RatePatternSize size{1024, 1024, 16, 16};
    const auto p = foveatedPattern(size, squareRegion(0.3f), squareRegion(0.7f));
    const std::uint32_t w = 64;
    CHECK(p[32 * w + 32] == kRateFull);
    CHECK(p[32 * w + 32 + 16] == kRateHalf); // x = 0.5
    CHECK(p[0] == kRateQuarter);
    CHECK(p[63 * w + 63] == kRateQuarter);
}

TEST_CASE("a texel the full-rate edge only grazes stays full rate") {
    // The circle reaches x = 0.26; the texel from x = 0.25 to 0.28125 touches it.
    const RatePatternSize size{1024, 1024, 16, 16};
    const FoveationRegion full = squareRegion(0.26f);
    const auto p = foveatedPattern(size, full, full);
    CHECK(p[32 * 64 + 40] == kRateFull);    // x from 0.25
    CHECK(p[32 * 64 + 41] == kRateQuarter); // x from 0.28125
}

TEST_CASE("an off-centre head-forward moves the region") {
    // Quest 3 eye L: head-forward sits right of the image centre and above it.
    const Fov quest3Left{-0.9425f, 0.6981f, 0.7679f, -0.9477f};
    const auto full = foveationRegion(quest3Left, Quat::identity(), 12.0f);
    REQUIRE(full.has_value());
    const RatePatternSize size{1024, 1024, 16, 16};
    const auto p = foveatedPattern(size, *full, *full);
    const auto first = std::find(p.begin(), p.end(), kRateFull) - p.begin();
    const std::uint32_t tx = static_cast<std::uint32_t>(first % 64);
    const std::uint32_t ty = static_cast<std::uint32_t>(first / 64);
    CHECK(tx > 32);
    CHECK(ty < 32);
}

TEST_CASE("the shares at each rate count pixels, clipped edge texels by their part") {
    // 20x20 pixels in 16 pixel texels: one whole texel, two of 16x4 and one of 4x4.
    const RatePatternSize size{20, 20, 16, 16};
    const auto shares = rateShares(size, {kRateFull, kRateHalf, kRateHalf, kRateQuarter});
    CHECK(shares.full == doctest::Approx(256.0 / 400.0));
    CHECK(shares.half == doctest::Approx(128.0 / 400.0));
    CHECK(shares.quarter == doctest::Approx(16.0 / 400.0));
    // A pattern of another size gives nothing.
    const auto none = rateShares(size, {kRateFull});
    CHECK(none.full == 0.0);
    CHECK(none.half == 0.0);
    CHECK(none.quarter == 0.0);
}

TEST_CASE("the eye test pattern coarsens a different half per eye") {
    const RatePatternSize size{64, 16, 16, 16};
    CHECK(eyeTestPattern(size, 0) ==
          std::vector<std::uint8_t>{kRateQuarter, kRateQuarter, kRateFull, kRateFull});
    CHECK(eyeTestPattern(size, 1) ==
          std::vector<std::uint8_t>{kRateFull, kRateFull, kRateQuarter, kRateQuarter});
}
