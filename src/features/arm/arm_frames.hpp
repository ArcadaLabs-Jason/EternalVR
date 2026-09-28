#pragma once

// Rigid frames for the off-hand arm (docs/VR_HANDS_HUD.md, "Off hand"): joint poses in the arms model's
// space, moved and compared as whole frames.
//
// A pose is a position and an axis whose rows are the joint's own x, y and z axes in model space
// (xr_math::ModelPose, id Tech's idMat3 convention; the game's GetJointTransforms hands them out this
// way). Everything here is pure and tested without the game.

#include "common/quat.hpp"
#include "common/vector.hpp"
#include "xr_math/offhand_pose.hpp"

namespace evr::arm {

using xr_math::IdViewAxis;
using xr_math::ModelPose;

// A direction in model space in the frame's own coordinates, and back.
Vec3 toLocal(const IdViewAxis& frame, Vec3 v);
Vec3 fromLocal(const IdViewAxis& frame, Vec3 local);

// `child` in the frame of `parent` (its offset and axis rows in the parent's coordinates), and the
// inverse: `local` placed under `parent`. compose(parent, relative(parent, child)) == child.
ModelPose relative(const ModelPose& parent, const ModelPose& child);
ModelPose compose(const ModelPose& parent, const ModelPose& local);

// The parent pose that puts a child with the offset `local` (relative(parent, child)) at `child`.
ModelPose parentFor(const ModelPose& child, const ModelPose& local);

// The frame whose first row is `direction` and whose second is `normal` made perpendicular to it (the
// third completes a right-handed frame). False for a zero direction or a normal parallel to it.
bool frameFrom(Vec3 direction, Vec3 normal, IdViewAxis& out);

// The axis turned by `radians` about the unit vector `axis` (right-hand rule).
IdViewAxis rotateAbout(const IdViewAxis& frame, Vec3 axis, float radians);
Vec3 rotateAbout(Vec3 v, Vec3 axis, float radians);

// The rotation that takes `from` to `to` (both orthonormal), as a quaternion acting on model-space
// vectors, and the angle of its twist about the unit vector `axis` (swing-twist split), in (-pi, pi].
Quat rotationBetween(const IdViewAxis& from, const IdViewAxis& to);
float twistAbout(const IdViewAxis& from, const IdViewAxis& to, Vec3 axis);

// The orientation of an axis as a quaternion (rotate(q, x) = rows.forward and so on), and back.
Quat toQuat(const IdViewAxis& axis);
IdViewAxis fromQuat(Quat q);

// Positions interpolated linearly and axes along the shortest arc; `weight` 0 gives `a`, 1 gives `b`.
ModelPose blendPose(const ModelPose& a, const ModelPose& b, float weight);

// Finite position and an orthonormal right-handed axis within `tolerance`.
bool plausiblePose(const ModelPose& pose, float tolerance = 0.02f);

} // namespace evr::arm
