#pragma once

// The joints of the game's first-person left arm that the off hand moves (docs/VR_HANDS_HUD.md,
// "Off hand"), found in the arms skeleton (md6/player/human/base/assets/mesh/marine.md6skl) by its shape
// from the game's own attach joint and checked by name (arm_skeleton.hpp).
//
// The arm hangs from its attach joint the other way round from a body skeleton: `lefthandattach` (a child
// of `origin`) carries `LeftHand` (the wrist), which carries the forearm's roll joints toward the elbow,
// then `LeftForeArm` (at the elbow) and last `LeftArm` (at the shoulder). Moving the attach joint alone
// carries the whole arm along rigidly, which is why the sleeve stretched from the body to the arm.
// [static: the skeleton's parent table, build 25216728 (the same in marine.md6skl, loaded, and
// arms.md6skl): 30 lefthandattach <- 0 origin, 31 LeftHand,
// 52 leftforearmroll3, 53 leftforearmroll2, 54 leftforearmroll1, 55 LeftForeArmRoll, 56 LeftForeArm,
// 57 LeftArm, each the child of the one before.]

#include "xr_math/offhand_pose.hpp"

#include <array>
#include <cstddef>

namespace evr::arm {

enum class ArmJoint : std::size_t {
    Attach,      // lefthandattach: the joint the game's weapon-lag modifier moves
    Hand,        // LeftHand: the wrist
    Roll3,       // leftforearmroll3: next to the wrist
    Roll2,       // leftforearmroll2
    Roll1,       // leftforearmroll1
    ForeArmRoll, // LeftForeArmRoll: next to the elbow
    ForeArm,     // LeftForeArm: the elbow
    UpperArm,    // LeftArm: the shoulder
};

inline constexpr std::size_t kArmJointCount = 8;

// The names, in ArmJoint order; each joint's parent is the one before it.
inline constexpr std::array<const char*, kArmJointCount> kArmJointNames{
    "lefthandattach",   "LeftHand",        "leftforearmroll3", "leftforearmroll2",
    "leftforearmroll1", "LeftForeArmRoll", "LeftForeArm",      "LeftArm"};

constexpr std::size_t index(ArmJoint joint) {
    return static_cast<std::size_t>(joint);
}

// Model-space poses of the arm's joints, indexed by ArmJoint.
using ArmPoses = std::array<xr_math::ModelPose, kArmJointCount>;

} // namespace evr::arm
