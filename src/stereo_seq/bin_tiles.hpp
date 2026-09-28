#pragma once

// The light and decal binning's screen tiles for a view's own projection
// (docs/rig-findings/stereo-bin-tiles.md).
//
// The binning setup (RVA 0x1CFC050) hands its compute passes the tile grid as four parameters built from
// the render view's fov_x and fov_y, which describe a symmetric frustum: binTileLeft = -tan(fov_x / 2),
// binTileTop = -tan(fov_y / 2), binTileWidth = 2 tan(fov_x / 2) * 32 / width and binTileHeight =
// 2 tan(fov_y / 2) * 32 / height (32-pixel tiles, in view-space tangents). Under an asymmetric explicit
// projection, which every Route S eye has, they describe another frustum than the one drawn, so lights and
// decals land in shifted tiles: lit areas end in tile-shaped steps. The values here come from the projection
// itself; for a symmetric projection they are the engine's.

#include "stereo_seq/centered_matrix.hpp"

#include <optional>

namespace evr::stereo_seq {

inline constexpr float kBinTilePixels = 32.0f;

struct BinTileParams {
    float width = 0.0f;  // tangent step per tile column
    float height = 0.0f; // tangent step per tile row
    float left = 0.0f;   // tangent at the left edge of column 0
    float top = 0.0f;    // tangent at the top edge of row 0, positive down
};

// The parameters for `projection` (the latched projectionMatrix, row-major, the layout of
// xr_math::engineProjection) drawn at `width` x `height` pixels; nullopt when the projection is not a
// perspective one or the size is empty. Row 0 is the image's top row, so `top` is -tan(up): measured on the
// rig, starting the rows from the down edge instead breaks both eyes.
std::optional<BinTileParams> binTileParams(const Matrix4& projection, int width, int height);

// The engine's own parameters for a symmetric frustum of `fovX` x `fovY` degrees (what RVA 0x1CFC050 sets).
BinTileParams engineBinTileParams(float fovXDegrees, float fovYDegrees, int width, int height);

} // namespace evr::stereo_seq
