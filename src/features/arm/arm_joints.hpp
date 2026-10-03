#pragma once

// The joints of one of the game's first-person arms that the layer moves (docs/VR_HANDS_HUD.md, "The whole
// arm" and "The weapon arm"), found in the arms skeleton (md6/player/human/base/assets/mesh/marine.md6skl)
// by its shape from the game's own attach joint and checked by name (arm_skeleton.hpp).
//
// The arm hangs from its attach joint the other way round from a body skeleton: `lefthandattach` (a child
// of `origin`) carries `LeftHand` (the wrist), which carries the forearm's roll joints toward the elbow,
// then `LeftForeArm` (at the elbow) and last `LeftArm` (at the shoulder). Moving the attach joint alone
// carries the whole arm along rigidly, which is why the sleeve stretched from the body to the arm. The right
// arm hangs the same way from `righthandattach`.
// [static: the skeleton's parent table, build 25216728 (the same in marine.md6skl, loaded, and
// arms.md6skl): 30 lefthandattach <- 0 origin, 31 LeftHand,
// 52 leftforearmroll3, 53 leftforearmroll2, 54 leftforearmroll1, 55 LeftForeArmRoll, 56 LeftForeArm,
// 57 LeftArm, each the child of the one before; 2 righthandattach <- 0 origin, 3 RightHand,
// 24 rightforearmroll3, 25 rightforearmroll2, 26 rightforearmroll1, 27 RightForeArmRoll, 28 RightForeArm,
// 29 RightArm, the same. The right arm's bind pose is the left's reflected in the model's x-z plane with
// each joint's three axes negated, so its joints keep right-handed frames: the right forearm lies along
// the wrist's +y, the left's along -y.]

#include "xr_math/offhand_pose.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace evr::arm {

// The model's arm: the left one is the off hand's, the right one holds the weapon (the arms model is not
// mirrored for the weapon in the left hand).
enum class ArmSide : std::uint8_t {
    Left,
    Right,
};

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

using ArmJointNames = std::array<const char*, kArmJointCount>;

// The names, in ArmJoint order; each joint's parent is the one before it.
inline constexpr ArmJointNames kLeftArmJointNames{"lefthandattach",   "LeftHand",         "leftforearmroll3",
                                                  "leftforearmroll2", "leftforearmroll1", "LeftForeArmRoll",
                                                  "LeftForeArm",      "LeftArm"};
inline constexpr ArmJointNames kRightArmJointNames{
    "righthandattach",   "RightHand",        "rightforearmroll3", "rightforearmroll2",
    "rightforearmroll1", "RightForeArmRoll", "RightForeArm",      "RightArm"};

constexpr const ArmJointNames& armJointNames(ArmSide side) {
    return side == ArmSide::Right ? kRightArmJointNames : kLeftArmJointNames;
}

constexpr std::size_t index(ArmJoint joint) {
    return static_cast<std::size_t>(joint);
}

// Model-space poses of the arm's joints, indexed by ArmJoint.
using ArmPoses = std::array<xr_math::ModelPose, kArmJointCount>;

} // namespace evr::arm
