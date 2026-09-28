#pragma once

// Where the full-rate region sits in each eye's image (ARCHITECTURE section 11).
//
// Fixed foveation centres the region on head-forward, not on the middle of the eye's image. Headset
// eye frusta are asymmetric (wider on the temporal side), so head-forward projects off-centre, toward
// the nasal side. Using the image centre instead would put the sharp area where the player is not
// looking.

#include "common/quat.hpp"
#include "xr_math/fov.hpp"

#include <optional>

namespace evr::foveation {

// An axis-aligned ellipse in Vulkan NDC (x right, y DOWN, both in [-1, 1] across the image).
// The radii differ per axis because NDC stretches the frustum's tangent extents to a square.
struct FoveationRegion {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float radiusX = 0.0f;
    float radiusY = 0.0f;
};

// Computes the full-rate region for one eye.
//
// `eyeOrientationInHead` is the eye's rotation relative to the head (identity unless the display is
// canted). The region is centred on the projection of head-forward and covers the whole angular cone
// around it. Off the eye's axis the cone projects to a tilted outline that is not centred on that
// point, so the radii come from the cone's boundary itself: its extent from the centre along each
// axis, then both grown by the same factor until the ellipse contains every boundary point. Erring
// larger costs a little performance, never sharpness where the player is looking.
//
// Returns nullopt if the half-angle is not in (0, 90) degrees, if the cone around head-forward
// reaches 90 degrees or more from the eye's axis (only possible with extreme canting), where the
// tangent plane cannot represent it, or if the eye FOV has no projection (see makeProjection).
std::optional<FoveationRegion>
fullRateRegion(const xr_math::Fov& eyeFov, const Quat& eyeOrientationInHead, float halfAngleDegrees);

} // namespace evr::foveation
