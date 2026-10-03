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
// (InitJointMods +0x20D, the hands in rbx) calls SetNum for six more per arm and then for the old count:
// the game allocates and owns the room, and nothing the game reads changes. The node is new, so no blend
// job has taken its lists yet (the tree build copies the current list's data pointer and count,
// 0x19BEEF0); growing them later, from the tick, could free a block a blend job still reads, which is why
// the layer never does.
//
// The attach joints. InitJointMods asks the global name table for "lefthandattach", "righthandattach" and
// "worldpropattach" (into [rsp+0x88], +0x90, +0x98) and has AddJointMod store their joints at idHands
// +0x28E4, +0x28E6 and +0x28E8 [static, build 25216728]. The weapon arm reads +0x28E6, so install checks
// that the second lookup names "righthandattach" and that the second AddJointMod takes its handle and
// stores at +0x28E6.
//
// Appending. At the tick each arm writes its six entries into that room in both lists and then raises
// both counts, the way SetNum does (addLayerMods); when the game builds new lists (a new map, a respawn)
// the hook makes room again and the entries are added again.
//
// Fail closed: install() checks InitJointMods, AddJointMod and SetNum byte by byte; the hook does nothing
// while the multiplayer guard refuses or the lists do not check out, and says so. Without the room the
// tick refuses the arm, as before.

#include "features/arm/arm_joints.hpp"
#include "features/arm/arm_skeleton.hpp"
#include "vkcore/game_text.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace evr::vkcore::controllers::offhand_mods {

// The layer's modifiers of one arm, in this order.
inline constexpr std::array<arm::ArmJoint, 6> kLayerJoints{arm::ArmJoint::Roll3,   arm::ArmJoint::Roll2,
                                                           arm::ArmJoint::Roll1,   arm::ArmJoint::ForeArmRoll,
                                                           arm::ArmJoint::ForeArm, arm::ArmJoint::UpperArm};
inline constexpr auto kLayerCount = static_cast<std::int32_t>(kLayerJoints.size());

// The arms the layer bends: the off hand's (the model's left arm, ETERNALVR_OFFHAND=free) and the weapon
// arm (its right arm, ETERNALVR_WEAPON_ARM=ik).
struct ArmSet {
    bool offHand = false;
    bool weapon = false;
};

// Finds InitJointMods and SetNum, checks them and AddJointMod (and, for the weapon arm, its attach
// joint), and hooks InitJointMods to make room for six modifiers per arm. The arms the room is made for;
// none (logged) leaves both the game's.
ArmSet install(const GameImage& image, ArmSet wanted);

// The hands' modifier node (idHands+0x2888, then +0x18), or nullptr.
const std::byte* modNode(const std::byte* hands);

// Whether the layer's modifiers for `joints` are at `base` in both lists of `node`.
bool layerModsInPlace(const std::byte* node, std::int32_t base, const arm::ArmJointIndices& joints);

enum class Problem : std::uint8_t {
    None,
    List,   // the lists do not check out, or a write failed
    NoRoom, // no room for the layer's six in both lists
    Taken,  // the game already modifies one of the layer's joints
};
struct AddError {
    Problem problem = Problem::None;
    std::string message;
};

// Appends the layer's modifiers for the arm on `side` to both lists (identity, the game's flags) and raises
// the counts; the first index, or -1 with `error` set.
std::int32_t
addLayerMods(const std::byte* node, arm::ArmSide side, const arm::ArmJointIndices& joints, AddError& error);

} // namespace evr::vkcore::controllers::offhand_mods
