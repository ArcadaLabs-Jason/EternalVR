#pragma once

// The off hand on the game's arms model (docs/VR_HANDS_HUD.md, "Off hand"): the left hand's joint
// modifier that puts the `lefthandattach` joint at the off-hand controller.
//
// The arms are one skinned model; the viewmodel hook places its origin and axis at the weapon hand
// (weapon_pose.hpp), so the off hand's target in model space is its offset from that placement, in the
// placement's forward / left / up axes. idHands::UpdateWeaponLagJointMods hands SetJointMod a translation
// and a rotation matrix for the joint as changes to its animated pose [static-verified: the translation is
// computed as the lagged position minus the animated one]: the position becomes animated + translation,
// and the joint's axis (rows, id Tech's idMat3) becomes animated * rotation [static: the blend's
// model-space pass 0x19E2A60 multiplies the modifier's quaternion on the left, and SetJointMod turns the
// matrix into the quaternion of its transpose; it matches the lag's row-vector convention]. The blend
// applies it after the joint's parent, so for a joint whose parent is also modified the change is on top
// of the parent's result (the arm's other joints use whole-pose overrides instead, vkcore/offhand_arm.hpp).
// So the modifier that reaches a target pose is
//     translation = target.position - animated.position
//     rotation    = transpose(animated.axis) * target.axis
// All positions in game units, axes as rows (forward, left, up) like IdViewAxis.

#include "common/vector.hpp"
#include "xr_math/head_view.hpp"
#include "xr_math/weapon_pose.hpp"

#include <array>
#include <optional>

namespace evr::xr_math {

// A row-major 3x3 matrix as the game stores idMat3: rows are the axes.
using Mat3Rows = std::array<float, 9>;

Mat3Rows toRows(const IdViewAxis& axis);
IdViewAxis fromRows(const Mat3Rows& m);
Mat3Rows multiply(const Mat3Rows& a, const Mat3Rows& b);
Mat3Rows transpose(const Mat3Rows& m);

struct ModelPose {
    Vec3 position;
    IdViewAxis axis;
};

// `target` (relative to the game's eye) in the space of a model placed at `model` (relative to the same
// eye).
ModelPose inModelSpace(const EyeRelativePose& model, const EyeRelativePose& target);

struct JointMod {
    Vec3 translation;
    Mat3Rows rotation{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
};

// The modifier that takes a joint from its animated pose to `target` (both in model space).
JointMod jointModToward(const ModelPose& animated, const ModelPose& target);

// The pose a modifier gives a joint animated at `animated` (a joint whose parent is not modified): the
// inverse of jointModToward, applyJointMod(a, jointModToward(a, t)) == t. The blend's model-space pass
// multiplies the modifier's quaternion on the left of the joint's [static: 0x19E2A60], which with
// SetJointMod's matrix-to-quaternion conversion (0x138CF10) is animated.axis * rotation in rows.
ModelPose applyJointMod(const ModelPose& animated, const JointMod& mod);

// The game's modifier and ours mixed: `weight` 0 gives the game's, 1 ours; the translation is
// interpolated linearly and the rotation along the shortest arc.
JointMod blendJointMods(const JointMod& game, const JointMod& ours, float weight);

// Checks before anything reaches the game: finite values, a rotation that is orthonormal to within
// `tolerance`, and a translation shorter than `maxLength` (game units).
bool plausibleJointMod(const JointMod& mod, float maxLength, float tolerance = 0.02f);

// A joint axis read from the game is usable: finite and orthonormal to within `tolerance`, and not the
// identity the game writes when it could not read the joint (with a zero position).
bool plausibleAnimatedPose(const ModelPose& pose, float tolerance = 0.02f);

} // namespace evr::xr_math
