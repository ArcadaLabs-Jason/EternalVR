#pragma once

// Where the full-rate and half-rate regions sit in each eye's image (ARCHITECTURE section 11).
//
// Fixed foveation anchors the regions on head-forward, not on the middle of the eye's image. Headset eye
// frusta are asymmetric (wider on the temporal side and at the bottom), so head-forward projects off-centre,
// toward the nasal side and the top. A circle around head-forward reaches the nasal edge and the top long
// before the temporal edge and the bottom: the reduced-rate band beyond it is a sliver on two sides and wide
// on the other two. Each region here reaches the same fraction of the way from head-forward to every edge
// of the image instead: four quarter ellipses that meet on the lines through head-forward, each side's
// radius that fraction of that side's extent.
//
// A preset's angle sets the fraction. The region has the area, on the eye's tangent plane, of the cone of
// that half-angle around the eye's axis (pi * tan^2 of the angle), so a preset shades about as much of the
// image at each rate as the cone did; in a symmetric square FOV the region is that cone's circle. Where the
// image's edge clipped the cone's circle, the region covers a little more: Quest 3 eye L (tangents 0.84
// nasal, 1.38 temporal, 0.97 up, 1.43 down), Subtle's 46 degree half-rate region 63.5% of the image against
// the clipped circle's 59.8%, so 36.5% at quarter rate instead of 40%; the other presets' circles fit.

#include "common/quat.hpp"
#include "xr_math/fov.hpp"

#include <optional>

namespace evr::foveation {

// The region in Vulkan NDC (x right, y DOWN, both in [-1, 1] across the image): head-forward's projection
// and the radius from it toward each edge of the image. A point is inside when (dx / rx)^2 + (dy / ry)^2 is
// at most 1, rx the left or right radius by the side of the centre it is on, ry the top or bottom one.
struct FoveationRegion {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float radiusLeft = 0.0f;
    float radiusRight = 0.0f;
    float radiusTop = 0.0f; // toward y = -1
    float radiusBottom = 0.0f;
};

// The same radius on every side (tests, the rate pattern's own cases).
constexpr FoveationRegion ellipseRegion(float centerX, float centerY, float radiusX, float radiusY) {
    return {centerX, centerY, radiusX, radiusX, radiusY, radiusY};
}

// The region of `halfAngleDegrees` for one eye. `eyeOrientationInHead` is the eye's rotation relative to the
// head (identity unless the display is canted); head-forward is projected with the eye's own projection.
//
// Returns nullopt if the half-angle is not in (0, 90) degrees, if the eye FOV has no projection (see
// makeProjection), or if head-forward is not inside the eye's image (only possible with extreme canting).
std::optional<FoveationRegion>
foveationRegion(const xr_math::Fov& eyeFov, const Quat& eyeOrientationInHead, float halfAngleDegrees);

} // namespace evr::foveation
