#pragma once

// The single centred camera that the engine renders from (ARCHITECTURE section 7).
//
// The engine culls and bins once, from one camera. For that to be safe for both eyes, the centred
// camera's frustum must contain everything either eye can see. This computes the tightest such FOV.
//
// The camera sits at the head origin and looks along head-space -Z. Eyes are offset (IPD) and may be
// rotated (canted displays), so an eye frustum pokes outside any centred frustum close to the face.
// The containment guarantee therefore only holds beyond a minimum depth, measured along the centre
// camera's forward axis; anything nearer than that is inside the player's head in practice.

#include "common/pose.hpp"
#include "xr_math/fov.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace evr::xr_math {

struct EyeView {
    Fov fov;
    // Eye pose relative to the head (the centre camera). Position is the eye offset in metres.
    Pose poseInHead;
};

enum class EnclosingShape : std::uint8_t {
    // Tightest fit; may be off-axis. Preferred because it wastes the fewest pixels, and the engine
    // camera is replaced by ours so it does not need to be symmetric.
    Asymmetric,
    // Widened to be symmetric about the forward axis, for engine paths that assume a symmetric
    // projection. Costs extra rendered area on the side the eyes do not reach.
    Symmetric,
};

// Returns the enclosing FOV, or nullopt when no finite one exists: minDepth is not positive, or a
// frustum edge of some eye does not point forward relative to the centre camera (a combined view of
// 180 degrees or more cannot be captured by one planar projection).
//
// Method: a point visible to an eye at centre depth >= minDepth lies in the region
//     { eyePosition + sum(k_i * cornerRay_i), k_i >= 0 }  intersected with  { depth >= minDepth }.
// The centre camera's horizontal tangent x / depth is a linear-fractional function with a positive
// denominator on that region, so its extremes lie at the region's vertices or along its unbounded
// edges. The vertices are where the four corner rays cross the depth = minDepth plane (or the eye
// position itself when the eye is already past that plane) and the unbounded edges are the corner
// rays, whose tangent is simply their direction's tangent. The same holds vertically. Taking the
// extremes over those eight candidates per eye gives the exact enclosing bounds.
std::optional<Fov> enclosingFov(std::span<const EyeView> eyes,
                                float minDepth,
                                EnclosingShape shape = EnclosingShape::Asymmetric);

} // namespace evr::xr_math
