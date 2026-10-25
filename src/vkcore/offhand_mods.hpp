#pragma once

// The hands' joint-modifier lists for the arms the layer bends (game_arm.hpp, which describes the lists;
// features/arm/mod_room.hpp): the free off hand's left arm and the weapon arm's right arm.
//
// Room. idHands::InitJointMods (RVA 0x138B080) takes a new modifier node from the animation pool and adds
// the three attach-joint modifiers with AddJointMod (0x138B360: int AddJointMod(idHands*, u16 name handle,
// u16 flags, int16* joint out), the joint looked up by name handle in the hands' skeleton, SetNum(node,
// num + 1), then the joint, the flags and an identity pose written into the current generation's list;
// the new index returned). SetNum (0x19A61F0: void SetNum(node*, int num)) grows each of the two lists
// that is too small to exactly `num` with idList Resize (0x4AAB80, the game's allocator), fills the new
// entries with no change (joint -1, flags 0) and sets both counts; a smaller count only sets the counts.
// So the lists end full, 3 in use and room for 3. A mid hook right after the third AddJointMod
// (InitJointMods +0x20D, the hands in rbx) calls SetNum for six more per arm (seven for the off hand
// with the bind solve: its wrist's change) and then for the old count: the game allocates and owns the room,
// and nothing the game reads changes. The node is new, so no blend job has taken its lists yet (the tree
// build copies the current list's data pointer and count, 0x19BEEF0); growing them later, from the tick,
// could free a block a blend job still reads, which is why the layer never does.
//
// The attach joints. InitJointMods asks the global name table for "lefthandattach", "righthandattach" and
// "worldpropattach" (into [rsp+0x88], +0x90, +0x98) and has AddJointMod store their joints at idHands
// +0x28E4, +0x28E6 and +0x28E8 [static, build 25216728]. The weapon arm reads +0x28E6, so install checks
// that the second lookup names "righthandattach" and that the second AddJointMod takes its handle and
// stores at +0x28E6.
//
// Appending. At the tick each arm writes its entries into that room in both lists and then raises
// both counts, the way SetNum does (addLayerMods); when the game builds new lists (a new map, a respawn)
// the hook makes room again and the entries are added again.
//
// The weight. InitJointMods hands the node's create (+0x10A, 0x497E00) the weight 0.95 (loaded at +0xB0 into
// its parameters' +0x2C), which the create copies to the owner's (idHands+0x2888) current weight (+0x3C) and
// target weight (+0x40); the owner's GetWeight (0x497DE0, just before the create) returns +0x3C, and
// SetWeight moves +0x40 and fades +0x3C toward it [static, build 25216728 and the Game Pass build, the same
// bytes]. The blend mixes every modifier of the node into the pose at that weight (features/arm/mod_mix.hpp;
// rig run ap-w1). modWeight() reads it for the layer to write past its targets (the weapon arm's glove;
// every joint with ETERNALVR_ARM_SOLVE=bind).
// Install checks the chain to the owner too: InitJointMods hands the create idHands+0x2878 (+0x103, lea rcx),
// the create puts it in rbx at +0xF (mov rbx, rcx), and at +0x5F, before copying, takes the owner from its
// +0x10.
//
// Fail closed: install() checks InitJointMods, AddJointMod and SetNum byte by byte; the hook does nothing
// while the multiplayer guard refuses or the lists do not check out, and says so. Without the room the
// tick refuses the arm, as before.

#include "features/arm/arm_joints.hpp"
#include "features/arm/arm_skeleton.hpp"
#include "features/arm/grip_hand.hpp"
#include "features/arm/mod_room.hpp"
#include "vkcore/game_text.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace evr::vkcore::controllers::offhand_mods {

// The layer's modifiers of one arm, in this order.
inline constexpr const std::array<arm::ArmJoint, arm::kModsPerArm>& kLayerJoints = arm::kLayerModJoints;
inline constexpr std::int32_t kLayerCount = arm::kModsPerArm;
// With the bind solve (ETERNALVR_ARM_SOLVE=bind) the off hand has a seventh, first: a change on its wrist
// (LeftHand) like the game's on the attach joint, which takes the rest of a turn the attach joint cannot make
// (features/arm/mod_mix.hpp, modifiedWithWrist); no change otherwise. The order is arm::layerModJoint's
// (features/arm/mod_room.hpp).

// The weapon arm also has the glove's (features/arm/grip_hand.hpp): an override on its wrist (RightHand)
// first, no change otherwise, and after the six an override on each of the fifteen finger bones
// (arm::kFingerMods), no change otherwise, so the glove can be put on the gun's grip.

// Whether the arm on `side` has a modifier on its wrist, first in its list: the off hand's change with the
// bind solve, the weapon arm's override.
bool hasWrist(arm::ArmSide side);

// The arm's modifiers on finger joints, after the six: arm::kFingerModCount for the weapon arm, and for the
// off hand with the weapon in the left hand or, with the bind solve, the free off hand (its hand poses,
// features/arm/hand_pose.hpp); none otherwise.
std::int32_t fingerCount(arm::ArmSide side);

// Whether the left arm has the glove's modifiers for the weapon in the left hand (left_hand_arms.hpp): both
// arms hold a glove on the gun. Not the free off hand's fingers, which have finger modifiers too.
bool leftGlove();

// The arm's modifiers on its own joints: the wrist's (when hasWrist) and the six.
std::int32_t armCount(arm::ArmSide side);

// All the layer's modifiers of the arm on `side`: armCount, then fingerCount.
std::int32_t layerCount(arm::ArmSide side);

// The joint of the arm's modifier `k` (0 to armCount - 1): the wrist first when it has one, then
// kLayerJoints.
arm::ArmJoint layerJoint(arm::ArmSide side, std::int32_t k);

// The arms the layer bends: the off hand's (the model's left arm, ETERNALVR_OFFHAND=free) and the weapon
// arm (its right arm, ETERNALVR_WEAPON_ARM=ik).
struct ArmSet {
    bool offHand = false;
    bool weapon = false;
};

// Finds InitJointMods and SetNum, checks them and AddJointMod (and, for the weapon arm, its attach
// joint), and hooks InitJointMods to make room for the modifiers of each arm (layerCount). The arms the room
// is made for; none (logged) leaves both the game's.
ArmSet install(const GameImage& image, ArmSet wanted);

// The hands' modifier node (idHands+0x2888, then +0x18), or nullptr.
const std::byte* modNode(const std::byte* hands);

// Rig diagnostics: the node's generation (+0x28; its low bit picks the list SetJointMod writes), or nullopt.
std::optional<std::int32_t> modGeneration(const std::byte* hands);

// The weight the blend mixes the hands' modifiers in with (the owner's current weight), when install()
// found where it comes from and both it and the target weight are usable (above 0.5, at most 1); otherwise
// nullopt, logged once. Game thread only.
std::optional<float> modWeight(const std::byte* hands);

// The owner's current and target weight as read, each finite and within 0..1, when install() found where
// they come from; otherwise nullopt (the weapon arm's glove, features/arm/grip_hand.hpp). Game thread only.
std::optional<arm::ModWeights> modWeights(const std::byte* hands);

// Whether the layer's modifiers of an arm, for the joints `ids` (layerCount of them, in list order), are at
// `base` in both lists of `node`.
bool layerModsInPlace(const std::byte* node, std::int32_t base, std::span<const std::int16_t> ids);

enum class Problem : std::uint8_t {
    None,
    List,   // the lists do not check out, or a write failed
    NoRoom, // no room for the layer's modifiers in both lists
    Taken,  // the game already modifies one of the layer's joints
};
struct AddError {
    Problem problem = Problem::None;
    std::string message;
};

// Sets the layer's modifiers of an arm (as for layerModsInPlace) to no change in both lists at once
// (arm::noChangeWrites), for the give-back after a multiplayer guard trip, after which no tick writes again.
// False when they are not in place or a write failed.
bool clearLayerMods(const std::byte* node, std::int32_t base, std::span<const std::int16_t> ids);

// Rig diagnostics: how many of those modifiers are overrides (flag 0x20) in each list, -1 for a list that
// cannot be read or does not hold them.
std::array<std::int32_t, 2>
overridesInLists(const std::byte* node, std::int32_t base, std::span<const std::int16_t> ids);

// Appends the layer's modifiers for the arm on `side`, for the joints `ids` (layerCount of them, in list
// order), to both lists (identity, the game's flags) and raises the counts; the first index, or -1 with
// `error` set.
std::int32_t
addLayerMods(const std::byte* node, arm::ArmSide side, std::span<const std::int16_t> ids, AddError& error);

} // namespace evr::vkcore::controllers::offhand_mods
