#include "features/foveation/rate_pattern.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace evr::foveation {

namespace {

std::uint32_t texels(std::uint32_t pixels, std::uint32_t perTexel) {
    return perTexel == 0 ? 0 : (pixels + perTexel - 1) / perTexel;
}

} // namespace

std::vector<std::uint8_t>
foveatedPattern(const RatePatternSize& size, const FoveationRegion& full, const FoveationRegion& half) {
    const std::uint32_t w = texels(size.targetWidth, size.texelWidth);
    const std::uint32_t h = texels(size.targetHeight, size.texelHeight);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h, kRateQuarter);
    const float pw = 2.0f / static_cast<float>(size.targetWidth);
    const float ph = 2.0f / static_cast<float>(size.targetHeight);
    for (std::uint32_t ty = 0; ty < h; ++ty) {
        const float y0 = -1.0f + static_cast<float>(ty * size.texelHeight) * ph;
        const float y1 = std::min(1.0f, y0 + static_cast<float>(size.texelHeight) * ph);
        for (std::uint32_t tx = 0; tx < w; ++tx) {
            const float x0 = -1.0f + static_cast<float>(tx * size.texelWidth) * pw;
            const float x1 = std::min(1.0f, x0 + static_cast<float>(size.texelWidth) * pw);
            std::uint8_t& rate = out[static_cast<std::size_t>(ty) * w + tx];
            if (touches(full, x0, x1, y0, y1)) {
                rate = kRateFull;
            } else if (touches(half, x0, x1, y0, y1)) {
                rate = kRateHalf;
            }
        }
    }
    return out;
}

RateShares rateShares(const RatePatternSize& size, const std::vector<std::uint8_t>& pattern) {
    const std::uint32_t w = texels(size.targetWidth, size.texelWidth);
    const std::uint32_t h = texels(size.targetHeight, size.texelHeight);
    RateShares out;
    if (w == 0 || h == 0 || pattern.size() != static_cast<std::size_t>(w) * h) {
        return out;
    }
    std::array<double, 3> pixels{};
    for (std::uint32_t ty = 0; ty < h; ++ty) {
        const std::uint32_t rows = std::min(size.texelHeight, size.targetHeight - ty * size.texelHeight);
        for (std::uint32_t tx = 0; tx < w; ++tx) {
            const std::uint32_t columns = std::min(size.texelWidth, size.targetWidth - tx * size.texelWidth);
            const std::uint8_t rate = pattern[static_cast<std::size_t>(ty) * w + tx];
            if (rate <= kRateQuarter) {
                pixels[rate] += static_cast<double>(rows) * columns;
            }
        }
    }
    const double total = static_cast<double>(size.targetWidth) * size.targetHeight;
    out.full = pixels[kRateFull] / total;
    out.half = pixels[kRateHalf] / total;
    out.quarter = pixels[kRateQuarter] / total;
    return out;
}

std::vector<std::uint8_t> eyeTestPattern(const RatePatternSize& size, int eye) {
    const std::uint32_t w = texels(size.targetWidth, size.texelWidth);
    const std::uint32_t h = texels(size.targetHeight, size.texelHeight);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h, kRateFull);
    for (std::uint32_t ty = 0; ty < h; ++ty) {
        for (std::uint32_t tx = 0; tx < w; ++tx) {
            const bool left = (tx * size.texelWidth + size.texelWidth / 2) < size.targetWidth / 2;
            if (left == (eye == 0)) {
                out[static_cast<std::size_t>(ty) * w + tx] = kRateQuarter;
            }
        }
    }
    return out;
}

} // namespace evr::foveation
