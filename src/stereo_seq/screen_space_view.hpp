#pragma once

// The view rotation the screen-space reflections march with, for a view's own (asymmetric) projection
// (docs/rig-findings/stereo-screen-space.md).
//
// The SSR/SSDO setup (RVA 0x1C71630) hands the screen-space passes the projection packed into four floats per
// direction: packedProjection = (0.5 m0 / m14, -0.5 m5 / m14, m10 / m14, m11 / m14) and
// packedInverseProjection0 = (2 / m0, -2 / m5, -1 / m0, 1 / m5) (with the inverse's own elements), i.e. the
// diagonal terms only. The shaders project a view-space point to the screen as uv = 0.5 + packed.xy * xy / z,
// with 0.5 fixed in the code, so the off-centre terms m2 and m6 of each eye's asymmetric frustum are lost.
// Inverse and projection still agree with each other: a pixel's reconstructed position comes back to the same
// pixel, in a view space sheared by S = [[1, 0, c], [0, 1, d], [0, 0, 1]] with c = m2 / m0 and d = m6 / m5.
// The reflection direction, though, is the world-space reflection turned into view space by the true rotation
// (three parameters whose xyz are the rotation's columns, set from inverseViewMatrixX/Y/Z), so the march ran
// along another screen line than the reflected ray: off by about 15 degrees on a Quest 3, sideways in
// opposite directions in the two eyes. Giving the march the rotation S * R (each column's x gets c times its
// z, y gets d times its z) makes the whole march the sheared image of the true one, which the packed
// projection maps to the true pixels. For a symmetric projection c = d = 0 and nothing changes.

#include "stereo_seq/centered_matrix.hpp"

#include <array>
#include <optional>

namespace evr::stereo_seq {

struct ViewShear {
    float x = 0.0f; // c = m2 / m0: view x gains c times view z
    float y = 0.0f; // d = m6 / m5: view y gains d times view z
};

// The shear between `projection` (the latched projectionMatrix, row-major, the layout of
// xr_math::engineProjection) and the centred projection the packed parameters describe; nullopt when the
// matrix is not a perspective projection of that shape (rows 0 and 1 with x, y and z terms only, row 3 only
// its z term).
std::optional<ViewShear> screenSpaceShear(const Matrix4& projection);

using ParamRow = std::array<float, 4>;

// Shears the three rotation rows (inverseViewMatrixX/Y/Z as the engine sets them; their xyz are the columns
// of the world-to-view rotation the shaders build) by `shear`: x += shear.x * z, y += shear.y * z. The w
// terms (the translation) are left as they are.
std::array<ParamRow, 3> shearRotationRows(const std::array<ParamRow, 3>& rows, const ViewShear& shear);

} // namespace evr::stereo_seq
