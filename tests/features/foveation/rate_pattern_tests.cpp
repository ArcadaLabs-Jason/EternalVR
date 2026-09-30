#include "features/foveation/rate_pattern.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using evr::foveation::eyeTestPattern;
using evr::foveation::foveatedPattern;
using evr::foveation::FoveationRegion;
using evr::foveation::kRateFull;
using evr::foveation::kRateHalf;
using evr::foveation::kRateQuarter;
using evr::foveation::RatePatternSize;

TEST_CASE("the pattern has one texel per block, rounded up") {
    const RatePatternSize size{100, 50, 16, 16};
    CHECK(foveatedPattern(size, {}, {}).size() == 7u * 4u);
    CHECK(foveatedPattern({0, 0, 16, 16}, {}, {}).empty());
}

TEST_CASE("the centre is full rate, a ring half rate, the corners quarter rate") {
    const RatePatternSize size{1024, 1024, 16, 16};
    const FoveationRegion full{0.0f, 0.0f, 0.3f, 0.3f};
    const FoveationRegion half{0.0f, 0.0f, 0.7f, 0.7f};
    const auto p = foveatedPattern(size, full, half);
    const std::uint32_t w = 64;
    CHECK(p[32 * w + 32] == kRateFull);
    CHECK(p[32 * w + 32 + 16] == kRateHalf); // x = 0.5
    CHECK(p[0] == kRateQuarter);
    CHECK(p[63 * w + 63] == kRateQuarter);
}

TEST_CASE("a texel the full-rate edge only grazes stays full rate") {
    // The ellipse reaches x = 0.26; the texel from x = 0.25 to 0.28125 touches it.
    const RatePatternSize size{1024, 1024, 16, 16};
    const FoveationRegion full{0.0f, 0.0f, 0.26f, 0.26f};
    const auto p = foveatedPattern(size, full, full);
    CHECK(p[32 * 64 + 40] == kRateFull);    // x from 0.25
    CHECK(p[32 * 64 + 41] == kRateQuarter); // x from 0.28125
}

TEST_CASE("an off-centre region follows its centre") {
    // Eye L's head-forward sits right of the image centre.
    const RatePatternSize size{1024, 1024, 16, 16};
    const FoveationRegion full{0.25f, -0.2f, 0.1f, 0.1f};
    const auto p = foveatedPattern(size, full, full);
    const auto first = std::find(p.begin(), p.end(), kRateFull) - p.begin();
    const std::uint32_t tx = static_cast<std::uint32_t>(first % 64);
    const std::uint32_t ty = static_cast<std::uint32_t>(first / 64);
    CHECK(tx > 32);
    CHECK(ty < 32);
}

TEST_CASE("the eye test pattern coarsens a different half per eye") {
    const RatePatternSize size{64, 16, 16, 16};
    CHECK(eyeTestPattern(size, 0) ==
          std::vector<std::uint8_t>{kRateQuarter, kRateQuarter, kRateFull, kRateFull});
    CHECK(eyeTestPattern(size, 1) ==
          std::vector<std::uint8_t>{kRateFull, kRateFull, kRateQuarter, kRateQuarter});
}
