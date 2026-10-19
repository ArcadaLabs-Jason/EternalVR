#pragma once

// The game's first-person arms bent by the layer (docs/VR_HANDS_HUD.md, "The whole arm" and "The weapon
// arm"): the left arm for the free off hand (offhand_hook.cpp), the right arm for the weapon arm
// (weapon_arm.cpp).
//
// The game moves each arm through one joint modifier, on its attach joint (`lefthandattach`,
// `righthandattach`), and the arm hangs from that joint the other way round from a body
// (features/arm/arm_joints.hpp), so the attach joint alone carries the arm along rigidly. To bend an arm,
// the layer adds six modifiers of its own to the hands' modifier list, one each for the forearm's four roll
// joints, the elbow (LeftForeArm, RightForeArm) and the shoulder end (LeftArm, RightArm), and writes them
// every tick with the game's own SetJointMod. The joints are found in the loaded skeleton by its shape from
// the game's own attach joint, and their names checked by comparing the skeleton's name handles with the
// handles the game's name table gives for the arm's names, the lookup idHands makes for the attach joints
// (arm_skeleton.hpp, game_arm_joints.hpp). A failure to read the skeleton logs, once per arm, the pointers
// followed to it and hex dumps of its header, name-handle table and the file's name block.
//
// The modifier list [static: idHands::InitJointMods 0x138B080, AddJointMod 0x138B360, SetJointMod
// 0x138CF10, idList::SetNum 0x19A61F0; build 25216728; the modifier pass is 0x19E1040, below]:
// idHands+0x2888 -> +0x18 is the list's node; +0x28 its generation, whose low bit picks one of two
// idList<jointMod_t> at +0x30 and +0x48 (data, int num at +8, int size at +0xC). A jointMod_t is 0x40
// bytes: quaternion, translation, scale, int16 joint at +0x30, int16 constraint parent at +0x32, uint16
// flags at +0x34. The blend applies a model-space modifier (flag 1) after the joint's parent, as a change
// (rotation on the left, translation added) or, with OVERRIDE (0x20), as the joint's whole model-space
// pose. The layer's six are overrides while the layer has the arm and no change (identity, the game's own
// flags) otherwise, so the game's animation runs untouched through them. The game's
// UpdateWeaponLagJointMods steps the generation every tick and writes its modifiers into the new list
// only, so the layer writes all of its own every tick too, as overrides or as no change. The modifiers are
// applied in list order, each against its parent as modified so far [static: 0x19E1040, which applies the
// hands' modifiers, Steam, one pass, round 4; the pass at 0x19E2A60, cited here before, skips them: it
// filters on flag 0x400], and the result mixed with the pose without them (features/arm/mod_mix.hpp). With
// the bind solve the off hand has a seventh, listed before its six: a change on the wrist (LeftHand) that
// takes the rest of a turn the attach joint cannot make, no change otherwise (offhand_mods.hpp). readArm
// reads the wrist live while that change has been no change for a few ticks (only the attach joint carries it
// then) and takes its offset from the attach joint again; after a change it carries the wrist by that offset
// (it held still in the rig's shotgun holds: 0.02 mm and 0.0007 degrees over a whole sweep; other weapons not
// measured).
//
// Two things the rig showed (runs ik5 and ik6, build 25216728). An override's translation is taken from
// the skeleton's root (joint 0, `origin`, about 1.6 below and 0.24 behind the space GetJointTransforms
// reads in), not from that space's origin: written as read, every overridden joint landed 1.5 lower, and
// the arm was a stretched tube. So writeArm subtracts the root's pose read this tick; the joints then read
// back within a centimetre of where they were written. And GetJointTransforms reads the blend's final pose,
// the layer's overrides of the last frame included, so the solver would feed on its own output. While an
// override may still show (kOverrideShowsMs and 3 ticks after the last), the layer's joints are taken as the
// last read without overrides placed them relative to the wrist, which the overrides do not move; and if
// they read back away from where they were written for kLandingTicks in a row, the arm is refused for good
// (logged).
//
// Adding them writes new entries past the end of both lists and then raises both counts, the way SetNum
// does, and only when both lists already have room (no reallocation while the blend job may read the
// other list); otherwise the arm is left to the game (logged). InitJointMods leaves the lists full, so
// the room is made there, by the game's SetNum, as the game builds them, six entries per arm
// (offhand_mods.hpp). When the game re-initialises its modifiers (a new map, a respawn) the node is new or
// the entries name other joints; that is noticed before any write and the entries are added again.
//
// Fail closed: install() checks the code the offsets come from; every tick readArm() checks the hands,
// the animator, the skeleton, the joints and the list before anything is read or written, and says why
// it refused (once per reason and arm). Each arm has its own joints, modifiers and state. Game thread
// (the off-hand hook) only.

#include "features/arm/arm_joints.hpp"
#include "features/arm/arm_solve.hpp"
#include "features/arm/grip_hand.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/offhand_mods.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::input {
enum class HandsCuff : std::uint8_t; // features/input/controller_settings.hpp
} // namespace evr::input

namespace evr::vkcore::controllers::game_arm {

// Checks the code the arms rely on beyond the hook's own checks: the animator getter
// UpdateWeaponLagJointMods calls at +0x94, and the offsets GetJointTransforms and SetJointMod use; then
// hooks InitJointMods to make room for the modifiers of the arms `wanted` (offhand_mods::install). The arms
// that can be used; none (logged) leaves both the game's.
offhand_mods::ArmSet install(const GameImage& image,
                             const std::byte* lagModsStart,
                             const std::byte* getJointTransforms,
                             const std::byte* setJointMod,
                             offhand_mods::ArmSet wanted);

struct ArmRead {
    arm::ArmPoses animated;  // this tick's animated model-space poses (GetJointTransforms, scaled)
    arm::ArmPoses read;      // the same as GetJointTransforms gave them, the layer's overrides included
    Vec3 scale;              // the animator's model scale: game units per internal unit, per axis
    Vec3 origin;             // the skeleton's root this tick, which overrides are placed from
    xr_math::ModelPose root; // the same with its axis (the rig dump's)
    // How far the read put the layer's joints from where they were last written (game units), or -1 when
    // no override of the last frames could show.
    float landed = -1.0f;
};

// The arm on `side` of `hands` as animated this tick, with the layer's modifiers in place. `attachJoint` is
// the joint the game's modifier is for; `attachAnimated`, when given, the pose the game read for it, which
// the read must match. Each call counts a tick toward a clean read, but with `countTick` false (a tick
// releaseArm counted already). nullopt when anything does not check out (logged once per reason).
std::optional<ArmRead> readArm(arm::ArmSide side,
                               const std::byte* hands,
                               std::int16_t attachJoint,
                               const xr_math::ModelPose* attachAnimated,
                               bool countTick = true);

// Writes the layer's modifiers of that arm: the forearm, elbow and shoulder joints of `poses` (model space,
// game units) as overrides. Each joint in `zeroScale` (bit n for arm::ArmJoint n, arm::handsOnly) also
// scales to nothing (the modifier's scale
// flag, 4, with the zero scale SetJointMod leaves in the entry [static: SetJointMod 0x138CF10 zeroes
// +0x20..+0x2B; the modifier pass 0x19E1040 copies the entry's scale (+0x20) over the joint's under an
// override with flag 4 (0x19E1486) and multiplies it in for a change (0x19E16D2), Steam, one pass, round 4,
// where 0x19E2A60 was cited before; rig: ap-hands2, ap2-hands-fold, r4-hands]): the skin bound to them
// shrinks into the joints (ETERNALVR_ARMS=hands). `landing`, when given, is where the poses written are meant
// to land after the blend's mix (written past them, features/arm/mod_mix.hpp): the read-back is measured
// against it instead of `poses`. An arm with the wrist's change (the off hand with the bind solve,
// offhand_mods::layerCount) also gets `wrist` (internal units; nullptr for no change) as that change, and the
// wrist's read-back counts in ArmRead::landed (not toward the landing guard). With `glove` (the weapon arm's
// glove on a grip) every modifier of the arm, the wrist's, the six and the fingers', is an override in its
// joint's parent's frame (arm::gloveWrites, features/arm/parent_frames.hpp), so the glove and the forearm
// ride the gun's pose of the frame the game draws, and the next read is measured against `landing` carried by
// the attach joint as read then; without `glove` the glove's modifiers are no change. False when the
// modifiers are no longer in place or a write failed.
bool writeArm(arm::ArmSide side,
              const std::byte* hands,
              const arm::ArmPoses& poses,
              Vec3 scale,
              Vec3 origin,
              std::uint32_t zeroScale = 0,
              const arm::ArmPoses* landing = nullptr,
              const xr_math::JointMod* wrist = nullptr,
              const arm::GripOverrides* glove = nullptr);

// The weapon arm's glove as GetJointTransforms reads it this tick, after readArm: each of its joints
// (arm::kRightHandJointNames) in its parent's frame.
struct HandRead {
    std::array<xr_math::ModelPose, arm::kHandJointCount> local;
};
// False when the arm has no glove modifiers, readArm did not run for these hands, or the read failed.
bool readHand(arm::ArmSide side, const std::byte* hands, HandRead& out);

// Development only (ETERNALVR_TEST_ARM_MOD_WEIGHT): holds the hands' joint-modifier weight at `weight`
// while the free off hand is posed. The modifier owner (idHands+0x2888) keeps its current weight at +0x3C and
// its target weight at +0x40, both 0.95 (offhand_mods.hpp), and the blend mixes every modifier of the node in
// at it (features/arm/mod_mix.hpp); both are written. The values found are kept and put back by
// releaseModWeight (the arm given back, a multiplayer guard trip). On other hands (a new level's) the values
// are found again and the old hands' are let go unwritten: those hands may be gone. False when nothing was
// written.
bool holdModWeight(const std::byte* hands, float weight);

// Puts back the weights holdModWeight found, on the hands it held them on; nothing when none are held.
void releaseModWeight(const std::byte* hands);

// The cuff hands only closes (ETERNALVR_TEST_HANDS_CUFF).
arm::Cuff cuffOf(input::HandsCuff cuff);

// Sets the layer's modifiers of that arm of `hands`, if it has them, back to no change. True when it did.
// Each call writes the list of the current generation (the game fills one a tick); for three calls after a
// pose, then none. `bothLists` writes both lists at once instead, into memory with no call into the game,
// even after a game call faulted (the give-back after a multiplayer guard trip, after which no tick runs).
bool releaseArm(arm::ArmSide side, const std::byte* hands, bool bothLists = false);

// Rig diagnostics: how many of the layer's modifiers of that arm are overrides (flag 0x20) in each of the two
// lists, or -1 when they cannot be read.
std::array<std::int32_t, 2> overridesLeft(arm::ArmSide side, const std::byte* hands);

// Whether one of the game's calls faulted this session (no call into either arm is made again).
bool gameCallFaulted();

// The hands the layer's modifiers of that arm were added to last, or nullptr before any.
const std::byte* armHands(arm::ArmSide side);

} // namespace evr::vkcore::controllers::game_arm
