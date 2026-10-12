#pragma once

// Where the full-rate and half-rate regions sit in each eye's image (ARCHITECTURE section 11).
//
// Fixed foveation centres the regions on head-forward, the direction the head faces, not on the middle of
// the eye's image. A region is the cone of the preset's half-angle around head-forward: a point of the eye's
// image is inside when its view ray, through the eye's own projection, is within that angle of head-forward.
// Both eyes use the head's forward and the same angle, so a direction in the scene gets the same rate in both
// eyes (a point at a finite distance nearly the same: the eyes see it from a few centimetres apart). With
// parallel eyes the cone meets the eye's image plane in a circle around head-forward's projection; with a
// canted display (the eye turned in the head) in an ellipse.
//
// Headset eye frusta are asymmetric (wider on the temporal side and at the bottom), so head-forward projects
// toward the nasal side and the top, and the image reaches less far from it there. Quest 3 eye L (-54, 40,
// 44, -54.3 degrees): Balanced's half-rate region of 40 degrees ends at the nasal edge, and the quarter-rate
// band beyond it is on the temporal side and at the top and bottom; eye R is its mirror image. Until T-128
// each region reached the same fraction of the way to every edge of its own eye's image instead: on that
// FOV eye L's full-rate region reached about 28 degrees to the left and eye R's about 18, so the same
// direction got different rates in the two eyes and each eye lost different detail.

#include "common/quat.hpp"
#include "common/vector.hpp"
#include "xr_math/fov.hpp"

#include <optional>

namespace evr::foveation {

// One region in one eye's image. Vulkan NDC (x right, y DOWN, both in [-1, 1] across the image) maps
// linearly onto the eye's tangent extents on the plane z = -1: x from `tangents.left` to `tangents.right`,
// y from `tangents.up` to `tangents.down`.
struct FoveationRegion {
    // Head-forward in the eye's space (x right, y up, the eye looking down -z), unit length, z < 0.
    Vec3 forward{0.0f, 0.0f, -1.0f};
    // In radians, in (0, pi / 2); 0 holds nothing.
    float halfAngle = 0.0f;
    xr_math::FovTangents tangents;
    // Head-forward's projection, in NDC (inside the image).
    float centerX = 0.0f;
    float centerY = 0.0f;
};

// The region of `halfAngleDegrees` for one eye. `eyeOrientationInHead` is the eye's rotation relative to the
// head (identity unless the display is canted); head-forward is the head's -z, expressed in the eye's space.
//
// Returns nullopt if the half-angle is not in (0, 90) degrees, if the eye FOV has no projection (see
// makeProjection), or if head-forward is not inside the eye's image (only possible with extreme canting).
std::optional<FoveationRegion>
foveationRegion(const xr_math::Fov& eyeFov, const Quat& eyeOrientationInHead, float halfAngleDegrees);

// The angle in radians between head-forward and the view ray through the NDC point (x, y).
double angleFromForward(const FoveationRegion& region, float x, float y);

// Whether the region holds the view ray of some point of the NDC rectangle [x0, x1] x [y0, y1], its edges
// included (x0 <= x1, y0 <= y1). False for a region of no angle or no image.
bool touches(const FoveationRegion& region, float x0, float x1, float y0, float y1);

} // namespace evr::foveation
