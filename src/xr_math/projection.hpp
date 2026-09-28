#pragma once

// Off-axis perspective projections for Vulkan clip space.
//
// Input space is a view space in the OpenXR convention: +X right, +Y up, looking down -Z.
// Output is Vulkan clip space, whose conventions differ from OpenGL in two ways that matter here:
//   - NDC +Y points DOWN the screen. The projection negates Y so that view-space "up" lands at the
//     top of the image (NDC y = -1). Doing this in the matrix, rather than with a negative viewport
//     height, keeps the viewport conventional and leaves the flip visible where it happens.
//   - NDC depth runs over [0, 1], not [-1, 1].
// The clip-space w is always the view-space distance in front of the camera (-z).

#include "common/mat4.hpp"
#include "xr_math/fov.hpp"

#include <cstdint>
#include <optional>

namespace evr::xr_math {

enum class DepthMode : std::uint8_t {
    // Near plane maps to depth 0, far plane to depth 1.
    Standard,
    // Near plane maps to depth 1 and depth approaches 0 at infinite distance. Float depth is densest
    // near 0, so this spends precision on distant geometry where standard Z has none, and needs no
    // far plane. This is what the OpenXR depth layer is given.
    ReversedInfinite,
};

struct ProjectionParams {
    Fov fov;
    float nearZ = 0.01f;
    // Ignored for DepthMode::ReversedInfinite.
    float farZ = 1000.0f;
    DepthMode depthMode = DepthMode::Standard;
};

// Builds the projection matrix. nearZ and farZ are positive distances; for Standard depth farZ must
// be greater than nearZ. Returns nullopt for inputs that have no finite projection: a half-angle at
// or beyond 90 degrees, a frustum with no width or height (left >= right or down >= up), a near plane
// that is not positive, a far plane not beyond it, or any NaN or infinity. FOVs come from the
// runtime, so a bad one must not turn into a matrix full of NaN.
std::optional<Mat4> makeProjection(const ProjectionParams& params);

// Convenience wrappers for the two depth modes.
std::optional<Mat4> makeProjectionStandard(const Fov& fov, float nearZ, float farZ);
std::optional<Mat4> makeProjectionReversedInfinite(const Fov& fov, float nearZ);

} // namespace evr::xr_math
