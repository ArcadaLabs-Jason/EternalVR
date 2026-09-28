#pragma once

// The arms skeleton's parents and joint-name handles, read from the game's loaded md6 skeleton
// (docs/VR_HANDS_HUD.md, "Off hand"), and the left arm's joints found in it.
//
// The layer reaches the skeleton the way idJointAnimator::GetJointTransforms does (the animator's model,
// +0x80, +0x310, then the data pointer at +0x60, whose u16 at +2 is the joint count it bounds indices
// with). At run time that data is an md6skl resource without its leading size word, up to its name block;
// for the first-person arms it is md6/player/human/base/assets/mesh/marine.md6skl (92 joints: the arms of
// arms.md6skl with the same indices, a forearm device of 11 joints under leftforearmroll1 and some camera
// joints), seen on the rig (run ik2: count 92, name block at 0x23E0) and matched in
// gameresources.resources. The joint names are not there as text (run ik1 read garbage where the file
// has them) but as 16-bit name handles, which the game's joint lookup by name compares [static: 0x19BFD40,
// called by idHands AddJointMod 0x138B360 with the handle InitJointMods 0x138B080 gets for "lefthandattach"
// from the global name table's vtable +0x38]:
//   +0x0  u16  offset of the name block in the file (the end of the tables here)
//   +0x2  u16  joint count
//   +0xC  u16  offset of the parent table: one int16 per joint, -1 for the root
//   +0x10 u16  offset of the name-handle table: one int16 handle per joint
//
// The joints are found by the skeleton's shape from the game's own attach joint, and the layer then checks
// their names through the game's name handles (vkcore/offhand_arm.cpp). Pure: the bytes come through a
// read callback, so the tests feed it a buffer.

#include "features/arm/arm_joints.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace evr::arm {

// Copies `size` bytes from `offset` in the skeleton data into `out`; false when they cannot be read.
using SkeletonRead = std::function<bool(std::size_t offset, void* out, std::size_t size)>;

inline constexpr std::size_t kSkeletonEndAt = 0x0;
inline constexpr std::size_t kSkeletonCountAt = 0x2;
inline constexpr std::size_t kSkeletonParentsAt = 0xC;
inline constexpr std::size_t kSkeletonNameHandlesAt = 0x10;
inline constexpr std::size_t kSkeletonHeaderSize = 0x12;
inline constexpr std::size_t kMaxJoints = 1024;

struct Skeleton {
    std::vector<std::int16_t> parents;     // -1 for a root; always less than the joint's own index
    std::vector<std::int16_t> nameHandles; // the game's name handle of each joint
};

// The parents and name handles, or nullopt with `error` saying what did not check out: a count outside
// 1..1024, a table that overlaps the header or runs past the tables' end, a parent not before its child,
// an unreadable table.
std::optional<Skeleton> readSkeleton(const SkeletonRead& read, std::string& error);

using ArmJointIndices = std::array<std::int16_t, kArmJointCount>;

// The left arm's joints from `attach` (the game's own left attach joint) by the skeleton's shape:
// LeftHand is the attach joint's only child, and the forearm is the one line of descent from LeftHand
// exactly six joints long that ends in a leaf (three rolls, LeftForeArmRoll, LeftForeArm, LeftArm; side
// branches along it are allowed); the fingers, the prop joint and the forearm device are shorter. nullopt
// with `error` otherwise.
std::optional<ArmJointIndices>
findArmJoints(const Skeleton& skeleton, std::int16_t attach, std::string& error);

// True when each joint's name handle is the handle of its name (kArmJointNames order); `error` names the
// first that is not.
bool namesMatch(const Skeleton& skeleton,
                const ArmJointIndices& joints,
                const std::array<std::int16_t, kArmJointCount>& handles,
                std::string& error);

} // namespace evr::arm
