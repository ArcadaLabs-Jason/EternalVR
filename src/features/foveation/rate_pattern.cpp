#include "features/foveation/rate_pattern.hpp"

#include <algorithm>
#include <cstddef>

namespace evr::foveation {

namespace {

std::uint32_t texels(std::uint32_t pixels, std::uint32_t perTexel) {
    return perTexel == 0 ? 0 : (pixels + perTexel - 1) / perTexel;
}

// Whether any point of the NDC rectangle [x0, x1] x [y0, y1] lies inside the ellipse: the rectangle's point
// nearest the centre, in the ellipse's own scaled space.
bool touches(const FoveationRegion& e, float x0, float x1, float y0, float y1) {
    if (e.radiusX <= 0.0f || e.radiusY <= 0.0f) {
        return false;
    }
    const float nx = std::clamp(e.centerX, x0, x1);
    const float ny = std::clamp(e.centerY, y0, y1);
    const float dx = (nx - e.centerX) / e.radiusX;
    const float dy = (ny - e.centerY) / e.radiusY;
    return dx * dx + dy * dy <= 1.0f;
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
