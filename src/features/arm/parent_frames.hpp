#pragma once

// The weapon arm's modifiers in each joint's parent's frame while the glove is on a grip
// (docs/VR_HANDS_HUD.md, "The glove on the grip", "In the gun's frame").
//
// The space of a joint modifier is its flag 1 [static: the modifier pass 0x19E1040, Steam, build 25216728;
// SetJointMod 0x138CF10 stores the flags it is handed at +0x34 as they are]. The pass applies the modifiers
// to a copy of the pose without them, each joint held in its parent's frame, in list order. With flag 1 the
// modifier is in model space: the pass builds the parent's model pose as modified so far (0x19E36A0) and
// turns the modifier into the joint's pose in that frame (an override: the rotation conj(parent) q, the
// translation taken from the parent's and turned back, 0x19E1309 to 0x19E15D3). Without flag 1 it is in the
// parent's frame: an override's rotation (flag 2), scale (4) and translation (8) become the joint's pose
// under its parent as they are (0x19E1D0F to 0x19E1D5C); a change multiplies its rotation in (0x19E1D75)
// and, with flag 0x10, turns the joint's offset about its translation (0x19E1E44), otherwise adds that with 8
// (0x19E1F60). The Game Pass build has the same code (its pass at 0x19EC8A0, the flag branches at the same
// distance). The blend then mixes each joint's pose in its parent's frame at the node's weight (mod_mix.hpp),
// so either way the joint lands mixed from its animated pose; what differs is the frame the written pose is
// held in.
//
// A model-space override lands where it was written whatever the parent does in that frame. The layer places
// the glove from the gun's root joint (the right attach joint, the glove's parent) as read, which is the last
// frame's: where the game's animation moves the gun within a frame (a shot's recoil, a mod coming up) the
// glove landed where the gun had been (the rig's ga-mod1: up to 15 cm in the first two seconds of the
// Ballista's shots), and with it the forearm, written in model space under the glove. Held in the parent's
// frame, the glove's wrist is placed by the gun's pose of the frame the game draws, the fingers by the
// wrist's and the forearm by the glove's, as the game's own animation carries the hand on every gun.
//
// Pure: no game memory, tested on its own.

#include "features/arm/arm_joints.hpp"
#include "features/arm/grip_hand.hpp"
#include "features/arm/mod_room.hpp"
#include "xr_math/offhand_pose.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace evr::arm {

// A joint modifier's flags [static: 0x19E1040]: model space (without it the parent's frame), rotation,
// scale, translation, and override (the joint's whole pose; without it a change). The pass does not read
// 0x200; the layer keeps it as it has always written it.
inline constexpr std::uint16_t kModModelSpace = 0x1;
inline constexpr std::uint16_t kModScale = 0x4;
inline constexpr std::uint16_t kModOverride = 0x20;
inline constexpr std::uint16_t kFlagsModelOverride = 0x22B;  // model space, rotation, translation, override
inline constexpr std::uint16_t kFlagsParentOverride = 0x22A; // the same in the parent's frame

// One modifier as the layer hands it to SetJointMod: the pose (its translation in the model's internal units)
// and the flags.
struct ModWrite {
    xr_math::ModelPose pose;
    std::uint16_t flags = kFlagsModelOverride;
};

// The weapon arm's modifiers with the glove on a grip: the wrist's, the six (kLayerModJoints), the fifteen
// fingers' (kFingerMods), in list order.
inline constexpr std::size_t kGloveWrites = 1 + static_cast<std::size_t>(kModsPerArm) + kFingerModCount;

// A model scale (game units per internal unit, per axis) the same on all three axes, to kUniformScale of
// each other. The scale is along the model's axes and an offset in its parent's frame is along the parent's
// turned axes, so only a uniform scale divides out of it.
inline constexpr float kUniformScale = 1e-4f;
bool uniformScale(Vec3 scale);

// Whether gloveWrites holds `glove` in its joints' parents' frames for the model scale `scale`: a uniform
// scale and a glove not in model space.
bool heldInParents(const GripOverrides& glove, Vec3 scale);

// Each of them as an override in its joint's parent's frame: the wrist under the attach joint `glove` was
// placed under (GripOverrides::attach), each forearm joint of `arm` (model space, game units: the wrist's
// forearm as written under the glove's wrist, the weapon arm's `written`) under the one before it as written
// (the first under the glove's wrist), each finger under its parent as written, the offsets divided by the
// model scale `scale` (the rig reads 1, 1, 1). With a scale that is not uniform (uniformScale), or a glove in
// model space (GripOverrides::modelSpace), each in model space instead, as the layer wrote them before (flag
// 1; the offset from the skeleton's root `origin` divided by the scale per axis), so the glove trails a fast
// gun by a frame but lands where it is written. A forearm joint in `zeroScale` (bit n for ArmJoint n,
// arm::handsOnly) also scales to nothing.
std::array<ModWrite, kGloveWrites> gloveWrites(
    const ArmPoses& arm, const GripOverrides& glove, Vec3 scale, Vec3 origin, std::uint32_t zeroScale = 0);

// The arm's poses `poses` (model space) placed under the attach joint `then`, carried to where the attach
// joint `now` puts them: where overrides held in their parents' frames land when the gun's root has moved.
ArmPoses carriedBy(const xr_math::ModelPose& then, const xr_math::ModelPose& now, const ArmPoses& poses);

} // namespace evr::arm
