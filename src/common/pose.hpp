#pragma once

// Rigid pose: an orientation and a position, equivalent to XrPosef.
//
// A pose maps points from its own (local) space into its parent space:
//     p_parent = rotate(orientation, p_local) + position
// So an eye pose "relative to head" takes eye-space points to head space, and its inverse is the
// eye's view transform.

#include "common/mat4.hpp"
#include "common/quat.hpp"
#include "common/vector.hpp"

namespace evr {

struct Pose {
    Quat orientation = Quat::identity();
    Vec3 position{};

    static constexpr Pose identity() { return {}; }
};

// compose(parent, child) is the pose of `child` expressed in the parent of `parent`. Applying the
// result to a point equals applying `child` first, then `parent`.
Pose compose(const Pose& parent, const Pose& child);

Pose inverse(const Pose& pose);

Vec3 transformPoint(const Pose& pose, Vec3 point);

// Directions ignore translation.
Vec3 transformDirection(const Pose& pose, Vec3 direction);

// Matrix form of the pose (local to parent).
Mat4 toMatrix(const Pose& pose);

// View matrix for a camera at `pose`: maps parent-space points into the camera's local space.
Mat4 toViewMatrix(const Pose& pose);

} // namespace evr
