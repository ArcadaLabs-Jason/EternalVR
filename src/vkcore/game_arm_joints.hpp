#pragma once

// Finding an arm's joints in the game's loaded arms skeleton (game_arm.hpp; features/arm/arm_skeleton.hpp):
// by the skeleton's shape from the game's own attach joint of that arm, the names checked against the
// handles the game's name table gives (the lookup idHands::InitJointMods makes for the attach joints), and,
// on the first failure per arm, the pointers followed and hex dumps of the skeleton's header, name handles,
// the file's name block and parent table in the log. Game thread (the off-hand hook) only.

#include "features/arm/arm_skeleton.hpp"
#include "vkcore/game_text.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace evr::vkcore::controllers::game_arm {

// What an arm's log lines start with and name: "offhand: arm" / "the off hand" for the left arm,
// "weapon arm" / "the weapon arm" for the right one.
struct SideText {
    const char* tag;     // the joints line: "<tag>: arm joints (...)"
    const char* prefix;  // every other line
    const char* subject; // "...; <subject> stays the game's"
    const char* side;    // "left" / "right"
};
const SideText& sideText(arm::ArmSide side);

// The pointers followed from the hands to the skeleton data (the way GetJointTransforms reaches it).
struct SkeletonPath {
    const std::byte* hands = nullptr;
    const std::byte* model = nullptr;     // hands +0x370
    const std::byte* animator = nullptr;  // model +0x4E0
    const std::byte* animModel = nullptr; // animator +0x8
    const std::byte* decl = nullptr;      // +0x80
    const std::byte* skeleton = nullptr;  // +0x310
    const std::byte* data = nullptr;      // +0x60
};

// Install time: the game's name table, found where InitJointMods asks it for "lefthandattach". Without it
// the joints are still found by the skeleton's shape, but their names go unchecked (logged).
void findNameTable(const GameImage& image, const char* tag);
// RVA of the name table's global, or 0 when it was not found.
std::uint32_t nameTableRva(const GameImage& image);

enum class JointsProblem : std::uint8_t {
    Skeleton, // the skeleton's tables do not check out
    Joints,   // the arm's shape is not there
    Names,    // a joint's name handle is not its name's
    Fault,    // the game's name lookup faulted (its data may be half-updated)
};

// The arm's joints from `attach` in the skeleton at `path.data`, logged once found; nullopt with `problem`
// and `error` set (the skeleton dumped, once per arm).
std::optional<arm::ArmJointIndices> findJoints(arm::ArmSide side,
                                               const SkeletonPath& path,
                                               std::int16_t attach,
                                               JointsProblem& problem,
                                               std::string& error);

// Once per arm, on its first failure to reach or read the skeleton: the pointers followed and the bytes
// found, so a rig log shows the real layout.
void dumpSkeleton(arm::ArmSide side, const SkeletonPath& path, std::int16_t attach);

} // namespace evr::vkcore::controllers::game_arm
