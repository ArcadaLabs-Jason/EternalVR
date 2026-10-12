#pragma once

// A shading rate image's texels for fixed foveation (ARCHITECTURE section 11): each texel covers a block of
// the render target's pixels and holds a palette index, 0 = full rate, 1 = one shade per 2x2 pixels, 2 = one
// per 4x4. The full-rate region and the wider half-rate one are cones around head-forward
// (foveation_region.hpp); a texel takes the finest rate any of its pixels needs, so the full-rate region is
// never coarsened.

#include "features/foveation/foveation_region.hpp"

#include <cstdint>
#include <vector>

namespace evr::foveation {

inline constexpr std::uint8_t kRateFull = 0;
inline constexpr std::uint8_t kRateHalf = 1;
inline constexpr std::uint8_t kRateQuarter = 2;

struct RatePatternSize {
    std::uint32_t targetWidth = 0; // the render target, in pixels
    std::uint32_t targetHeight = 0;
    std::uint32_t texelWidth = 16; // pixels per rate image texel
    std::uint32_t texelHeight = 16;
};

// Row-major texels, ceil(target / texel) in each direction: kRateFull where a texel touches `full`,
// kRateHalf where it touches `half` (which should contain `full`), kRateQuarter elsewhere. Empty for a zero
// size.
[[nodiscard]] std::vector<std::uint8_t>
foveatedPattern(const RatePatternSize& size, const FoveationRegion& full, const FoveationRegion& half);

// The share of the target's pixels at each rate in `pattern` (foveatedPattern's texels for `size`); all zero
// for a pattern of another size.
struct RateShares {
    double full = 0.0;
    double half = 0.0;
    double quarter = 0.0;
};
[[nodiscard]] RateShares rateShares(const RatePatternSize& size, const std::vector<std::uint8_t>& pattern);

// A test pattern: kRateQuarter on the left half of the target for eye 0, on the right half for eye 1,
// kRateFull elsewhere, to see in captures which eye each render used.
[[nodiscard]] std::vector<std::uint8_t> eyeTestPattern(const RatePatternSize& size, int eye);

} // namespace evr::foveation
