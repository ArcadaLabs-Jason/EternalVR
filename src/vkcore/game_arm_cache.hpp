#pragma once

// What game_arm.cpp (install, the read) and game_arm_write.cpp (the writes and the give-back) share: each
// arm's cache of joints, modifiers and write state, the game's SetJointMod, and the session's fault flag
// (game_arm.hpp). Internal to those two files; game thread (the off-hand hook) only.

#include "features/arm/arm_joints.hpp"
#include "features/arm/arm_skeleton.hpp"
#include "features/arm/grip_hand.hpp"
#include "vkcore/offhand_mods.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <windows.h>

namespace evr::vkcore::controllers::game_arm::detail {

constexpr std::uint16_t kFlagsChange = 0x20B;   // model space, rotation, translation, reference pose
constexpr std::uint16_t kFlagsOverride = 0x22B; // the same, as the joint's whole pose
constexpr std::uint16_t kFlagScale = 0x4;       // with the entry's scale (zero after SetJointMod)
constexpr std::int32_t kTicksCap = 1 << 20;

// The most modifiers one arm has: the wrist's, the six and the glove's fingers.
constexpr std::size_t kMaxLayerMods = 1 + offhand_mods::kLayerCount + arm::kFingerModCount;

using SetJointModFn = void(__fastcall*)(void* hands,
                                        std::int32_t mod,
                                        std::int16_t joint,
                                        const float* translation,
                                        const float* rotation,
                                        std::uint16_t flags);

// One per arm.
struct Cache {
    const std::byte* skeletonData = nullptr;
    std::int16_t attach = -1;
    arm::ArmJointIndices joints{};
    arm::HandJointIndices hand{}; // the weapon arm's glove: the wrist's finger joints
    // The joints of the layer's modifiers in list order (offhand_mods::layerCount of them).
    std::array<std::int16_t, kMaxLayerMods> ids{};
    bool jointsOk = false;
    const std::byte* animator = nullptr; // the last readArm's
    const std::byte* hands = nullptr;
    const std::byte* node = nullptr;
    std::int32_t base = -1; // the first of the layer's modifiers in both lists
    // The layer's joints relative to the wrist, from the last read the layer's overrides had not reached.
    arm::ArmPoses fromHand{};
    xr_math::ModelPose handFromAttach{}; // the wrist relative to the attach joint (the bind solve's wrist)
    bool fromHandOk = false;
    ULONGLONG lastOverride = 0;          // GetTickCount64 of the last override written
    std::int32_t ticksSinceOverride = 0; // ticks (readArm or releaseArm calls) since the last write
    std::int32_t wristQuietTicks = 0;    // writes in a row with the wrist's change as no change
    std::int32_t releaseWrites = 0;      // no-change writes since the last pose
    arm::ArmPoses written{};             // the poses last written
    // With the glove on a grip the last write was in each joint's parent's frame (parent_frames.hpp), placed
    // under this attach joint: the read is measured against `written` carried by the attach joint read then.
    std::optional<xr_math::ModelPose> writtenUnder;
    // Its shoulder end in model space (GripOverrides::modelShoulder): not carried by the attach joint.
    bool modelShoulder = false;
    std::int32_t offTicks = 0; // ticks in a row the read put an override away from where it was written
    bool offForGood = false;
    bool collapsed = false; // the last write scaled the layer's joints to nothing (hands only)
    std::uint32_t loggedReasons = 0;
};

Cache& cacheOf(arm::ArmSide side);

// The layer's modifiers' joints of the arm, in list order.
std::span<const std::int16_t> idsOf(const Cache& c, arm::ArmSide side);

// The game's SetJointMod, once install() found it; nullptr before.
SetJointModFn setJointMod();

// Whether a call into the game faulted this session; callFaulted() records one on `side` (logged once), after
// which no call into either arm is made again.
bool faulted();
void callFaulted(arm::ArmSide side, const char* call);

} // namespace evr::vkcore::controllers::game_arm::detail
