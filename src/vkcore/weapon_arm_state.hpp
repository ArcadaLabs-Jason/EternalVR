#pragma once

// The weapon arm's per-arm state and its trace (weapon_arm.hpp): internal to weapon_arm.cpp and
// weapon_arm_trace.cpp. Off-hand hook thread (the game thread running idHands::Update) only.

#include "features/arm/grip_hand.hpp"
#include "features/input/offhand_policy.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/weapon_arm.hpp"
#include "xr_math/offhand_pose.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::vkcore::controllers::weapon_arm::detail {

// One arm holding a glove (off-hand hook thread only): the weapon arm, and with the weapon in the left hand
// the left arm too (holding()).
struct ArmState {
    arm::ArmSide side;
    // Type info (build 25216728): the arm's attach joint's index, stored by InitJointMods' AddJointMod (the
    // right one's checked at install, offhand_mods.hpp).
    std::size_t attachJoint;
    const char* tag;
    input::ArmBlend blend;
    input::ArmDecision lastDecision{false, input::ArmReason::ModeGame};
    bool traced = false;
    std::uint64_t traceLines = 0;
    ULONGLONG lastPoseTrace = 0;
    bool loggedBones = false;
    std::atomic<std::uint64_t> rejected{0};
    // The hands the arm was last posed on, and whether the one give-back after a multiplayer guard trip ran.
    const std::byte* posedHands = nullptr;
    bool tripReleased = false;
    // The arm's twist of the last tick it was solved (arm_solve.hpp), until the arm is given back.
    std::optional<float> lastTwist;

    // The glove on the grip (features/arm/grip_hand.hpp): the grip held this tick (the held item's
    // [right_grip] row's), the last one held while the grip eases out, the grip's weight (0 to 1, eased in
    // and out over ETERNALVR_OFFHAND_BLEND as there is a grip or not), the wrist's animated pose and the idle
    // hand's fingers as the last clean read had them.
    std::optional<arm::GripPose> held;
    arm::GripPose activeGrip = arm::heavyCannonGrip();
    bool hasActive = false;
    bool modelSpace = false; // the glove written in model space (holding(): a glove on a controller)
    std::optional<xr_math::ModelPose> wristTarget; // the glove's wrist there (model space), placed each tick
    std::optional<xr_math::ModelPose> attachRead;  // the arm's attach joint as read this tick
    float gripRamp = 0.0f;
    arm::WristTracker wrist;
    std::array<xr_math::ModelPose, arm::kHandJointCount> idleHand{};
    bool idleHandOk = false;
    const std::byte* gloveHands = nullptr;
    float gloveCurl = 0.0f; // the last tick's, for the trace
    // The game's own grip of the arms' item `item` (the tick's) as the last clean read had it (capture()).
    std::optional<Capture> captured;
    const std::byte* item = nullptr;
    std::uint32_t captures = 0;
    // The hands' modifier weights this tick, how the glove is written for them and its settle weight
    // (tick()).
    std::optional<arm::ModWeights> weights;
    arm::GloveWeight gloveWeight;
    arm::GloveSettle settle;
    float settleWeight = 0.0f;
    // Trace only: the glove's wrist read back against where it was meant to land the tick before (game
    // units, the largest since the last trace line), and how many reads showed the write of 1, 2 or 3 ticks
    // before.
    std::optional<xr_math::ModelPose> lastWanted;
    // Trace only: the gun's root joint as read the tick the last glove was written under it.
    std::optional<xr_math::ModelPose> lastAttach;
    // The forearm's first joint where it was meant to land, in the wrist's frame.
    xr_math::ModelPose lastSleeve{};
    Vec3 lastShoulder{};                  // the shoulder end where it was meant to land (model space)
    std::optional<Vec3> lastShoulderRead; // the shoulder end as read the tick before (model space)
    std::uint64_t tickLines = 0;
    float landedMax = 0.0f;
    std::array<unsigned, 3> readLags{};
    // Trace only: ticks on one modifier node, the ticks its generation's low bit (the list SetJointMod
    // writes) changed since the tick before, and the largest step.
    std::optional<std::int32_t> lastGeneration;
    unsigned generationTicks = 0;
    unsigned generationSteps = 0;
    std::int32_t generationJump = 0; // the largest move
    const std::byte* generationNode = nullptr;
};

// The policy's decision for the arm, logged when it changes (up to 400 lines; ETERNALVR_OFFHAND_TRACE).
void trace(ArmState& s, const input::ArmDecision& d, float weight);

// Once a second with ETERNALVR_OFFHAND_TRACE: whether the arm's pose line is due.
bool poseTraceDue(ArmState& s, const input::ControllerSettings& cfg);

// Trace only, every tick the glove was written the tick before: its wrist as read back (`miss`, game units,
// in the gun's root frame as read now) against where it was meant to land, and how far the root moved
// between the read the write was placed under and the frame the game drew it in (this read): at the grip,
// at the root, and its turn; and the forearm's first joint (the sleeve's end at the glove) as read back in
// the wrist's frame against where it was meant to land (`sleeve`, game units), and the shoulder end as read
// back (`shoulderRead`, model space) against where it was meant to land and against its read of the tick
// before (the frame drawn before).
void traceTick(ArmState& s, const xr_math::ModelPose& attach, float miss, float sleeve, Vec3 shoulderRead);

} // namespace evr::vkcore::controllers::weapon_arm::detail
