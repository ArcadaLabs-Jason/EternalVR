#include "stereo_seq/bin_tiles.hpp"

#include <cmath>
#include <numbers>

namespace evr::stereo_seq {

std::optional<BinTileParams> binTileParams(const Matrix4& projection, int width, int height) {
    const float m0 = projection[0];
    const float m2 = projection[2];
    const float m5 = projection[5];
    const float m6 = projection[6];
    if (width <= 0 || height <= 0 || !std::isfinite(m0) || !std::isfinite(m2) || !std::isfinite(m5) ||
        !std::isfinite(m6) || !(m0 > 1e-4f) || !(m5 > 1e-4f)) {
        return std::nullopt;
    }
    // x_ndc = m0 * tan - m2 over the frustum's tangents (xr_math::fovOfEngineProjection): -1 at the left
    // edge, +1 at the right; y likewise with +1 at the up edge.
    const float left = (m2 - 1.0f) / m0;
    const float right = (m2 + 1.0f) / m0;
    const float up = (m6 + 1.0f) / m5;
    const float down = (m6 - 1.0f) / m5;
    BinTileParams p;
    p.width = (right - left) * kBinTilePixels / static_cast<float>(width);
    p.height = (up - down) * kBinTilePixels / static_cast<float>(height);
    p.left = left;
    p.top = -up;
    return p;
}

BinTileParams engineBinTileParams(float fovXDegrees, float fovYDegrees, int width, int height) {
    constexpr float kHalfRadians = std::numbers::pi_v<float> / 360.0f;
    const float tanX = std::tan(fovXDegrees * kHalfRadians);
    const float tanY = std::tan(fovYDegrees * kHalfRadians);
    BinTileParams p;
    p.width = 2.0f * tanX * kBinTilePixels / static_cast<float>(width);
    p.height = 2.0f * tanY * kBinTilePixels / static_cast<float>(height);
    p.left = -tanX;
    p.top = -tanY;
    return p;
}

} // namespace evr::stereo_seq
