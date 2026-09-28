#pragma once

// The whole left arm for the free off hand (docs/VR_HANDS_HUD.md, "Off hand"; offhand_hook.cpp).
//
// The game moves the left arm through one joint modifier, on `lefthandattach`, and the arm hangs from that
// joint the other way round from a body (features/arm/arm_joints.hpp), so the attach joint alone carries
// the arm along rigidly. To bend it, the layer adds six modifiers of its own to the hands' modifier list,
// one each for the forearm's four roll joints, the elbow (LeftForeArm) and the shoulder end (LeftArm), and
// writes them every tick with the game's own SetJointMod. The joints are found in the loaded skeleton by
// its shape from the game's own attach joint, and their names checked by comparing the skeleton's name
// handles with the handles the game's name table gives for kArmJointNames, the lookup idHands makes for
// "lefthandattach" (arm_skeleton.hpp). A failure to read the skeleton logs, once, the pointers followed to
// it and hex dumps of its header, name-handle table and the file's name block.
//
// The modifier list [static: idHands::InitJointMods 0x138B080, AddJointMod 0x138B360, SetJointMod
// 0x138CF10, idList::SetNum 0x19A61F0, the blend's joint-modifier pass 0x19E2A60; build 25216728]:
// idHands+0x2888 -> +0x18 is the list's node; +0x28 its generation, whose low bit picks one of two
// idList<jointMod_t> at +0x30 and +0x48 (data, int num at +8, int size at +0xC). A jointMod_t is 0x40
// bytes: quaternion, translation, scale, int16 joint at +0x30, int16 constraint parent at +0x32, uint16
// flags at +0x34. The blend applies a model-space modifier (flag 1) after the joint's parent, as a change
// (rotation on the left, translation added) or, with OVERRIDE (0x20), as the joint's whole model-space
// pose. The layer's six are overrides while the controller has the arm and no change (identity, the
// game's own flags) otherwise, so the game's animation runs untouched through them.
//
// Two things the rig showed (runs ik5 and ik6, build 25216728). An override's translation is taken from
// the skeleton's root (joint 0, `origin`, about 1.6 below and 0.24 behind the space GetJointTransforms
// reads in), not from that space's origin: written as read, every overridden joint landed 1.5 lower, and
// the arm was a stretched tube. So writeArm subtracts the root's pose read this tick; the joints then read
// back within a centimetre of where they were written. And GetJointTransforms reads the blend's final pose,
// the layer's overrides of the last frame included, so the solver would feed on its own output. While an
// override may still show (kOverrideShowsMs after the last), the layer's joints are taken as the last read
// without overrides placed them relative to LeftHand, which the overrides do not move; and if they read back
// away from where they were written for kLandingTicks in a row, the arm is refused for good (logged).
//
// Adding them writes new entries past the end of both lists and then raises both counts, the way SetNum
// does, and only when both lists already have room (no reallocation while the blend job may read the
// other list); otherwise the arm is left to the game (logged). InitJointMods leaves the lists full, so
// the room is made there, by the game's SetNum, as the game builds them (offhand_mods.hpp). When the game
// re-initialises its modifiers (a new map, a respawn) the node is new or the entries name other joints;
// that is noticed before any write and the entries are added again.
//
// Fail closed: install() checks the code the offsets come from; every tick readArm() checks the hands,
// the animator, the skeleton, the joints and the list before anything is read or written, and says why
// it refused (once per reason). Game thread (the off-hand hook) only.

#include "features/arm/arm_joints.hpp"
#include "vkcore/game_text.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::vkcore::controllers::offhand_arm {

// Checks the code the arm relies on beyond the hook's own checks: the animator getter
// UpdateWeaponLagJointMods calls at +0x94, and the offsets GetJointTransforms and SetJointMod use; then
// hooks InitJointMods to make room for the layer's modifiers (offhand_mods::install). False (logged) leaves
// the off hand the game's.
bool install(const GameImage& image,
             const std::byte* lagModsStart,
             const std::byte* getJointTransforms,
             const std::byte* setJointMod);

struct ArmRead {
    arm::ArmPoses animated; // this tick's animated model-space poses (GetJointTransforms, scaled)
    Vec3 scale;             // the animator's model scale: game units per internal unit, per axis
    Vec3 origin;            // the skeleton's root this tick, which overrides are placed from
};

// The arm of `hands` as animated this tick, with the layer's modifiers in place. `attachJoint` is the
// joint the game's modifier is for, `attachAnimated` the pose the game read for it. nullopt when anything
// does not check out (logged once per reason).
std::optional<ArmRead>
readArm(const std::byte* hands, std::int16_t attachJoint, const xr_math::ModelPose& attachAnimated);

// Writes the layer's modifiers: the forearm, elbow and shoulder joints of `poses` (model space, game
// units) as overrides. False when the modifiers are no longer in place or a write failed.
bool writeArm(const std::byte* hands, const arm::ArmPoses& poses, Vec3 scale, Vec3 origin);

// Sets the layer's modifiers of `hands`, if it has them, back to no change.
void releaseArm(const std::byte* hands);

} // namespace evr::vkcore::controllers::offhand_arm
