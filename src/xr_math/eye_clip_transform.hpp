#pragma once

// Per-eye clip-space transform (ARCHITECTURE section 7).
//
// The engine produces clip-space positions for the centred camera: C_c = P_c * V_c * world. To get
// the same vertex in an eye's clip space without touching the engine's matrices, the patched shaders
// apply one extra 4x4:
//     C_e = P_e * V_e * V_c^-1 * P_c^-1 * C_c
// This is exact, including for canted displays and for depth, as long as both projections are
// invertible (true for every projection makeProjection builds).

#include "common/mat4.hpp"

#include <optional>

namespace evr::xr_math {

// Returns nullopt if the centre projection or view is singular.
std::optional<Mat4>
eyeClipTransform(const Mat4& centerProj, const Mat4& centerView, const Mat4& eyeProj, const Mat4& eyeView);

} // namespace evr::xr_math
