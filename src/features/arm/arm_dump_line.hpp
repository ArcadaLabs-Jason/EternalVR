#pragma once

// One line of the hands rig's dump (ETERNALVR_TEST_ARM_DUMP, docs/rig-findings/hands-rig.md): what the
// layer read and wrote for both arms at one moment, as one JSON object on one line, for the rig checker
// (check_hands.py). Rig use only.
//
// The line, keys in this order (numbers with 5 decimals; `null` for what is not known at that moment):
//   t       seconds, QueryPerformanceCounter over its frequency (the clock PowerShell's Stopwatch reads)
//   upm     game units per metre; scale: the arms animator's model scale (x y z)
//   fault   the planted fault in force: "none", "twist" or "forearm" (ETERNALVR_TEST_ARM_FAULT)
//   input   {"L": hand, "R": hand}, the scripted input as the layer read it: {"yaw", "pitch", "roll"
//           (degrees), "pos" [x y z] (metres from the head, LOCAL; null for the side's rest position)}, or
//           null for a hand the file gives no aim
//   arms    {"L": arm, "R": arm}: {"posing": "layer" or "game", "weight" (the layer's share, 0..1),
//           "twist" (degrees, the wrist's twist the solve found), "landed" (game units, how far the last
//           read put the layer's joints from where they were written; -1 when none could show),
//           "final" (the poses the layer wrote this tick), "read" (the poses GetJointTransforms gave this
//           tick, the blend's result of the last frame), "animated" (the game's own pose the solve
//           started from), "target" (the wrist pose wanted; null for the weapon arm, whose wrist stays the
//           game's), "root" (the skeleton's root joint as read: overrides are placed from it), "zero" (the
//           joints written scaled to nothing, hands only: bit n for the n-th role below, 0 for none),
//           "fingers" (the free off hand's hand poses, features/arm/hand_pose.hpp: {"curl" [thumb, index,
//           middle, ring, little] 0..1, "want" [15] each finger bone's turn in its parent's frame as written,
//           degrees, in kFingerMods order, "read" [15] the same as GetJointTransforms gave it this tick, or
//           null}; null while the arm's fingers are not posed)}. A pose set is an object of the eight joints
//           by role: attach, hand, roll3, roll2, roll1, forearmroll, forearm (the elbow), upperarm (the
//           shoulder); a pose is 12 numbers: position x y z, then the axis rows forward, left, up. Model
//           space of the arms model, game units. Joint poses are null while the game poses the arm.
//   model   the arms model's placement: {"eye" [x y z] world, "offset" [x y z] from the eye, "axis" [9]};
//           world = eye + offset + axis rows times the model-space position
//   head    the rendered head's axis rows [9] (world), for mirroring in its yaw frame
//   item    the held item's decl name ("weapon/player/shotgun"), "" for none
//   weapon  the weapon's root: the right arm's attach joint (righthandattach) as read, a pose of 12
//   aim     the ray shots take: {"origin" [x y z] from the eye, "dir" [x y z] world}
//   grip    the weapon hand's grip position from the eye [x y z]; offgrip the off hand's, or null
//   arms_item  the item the arms are placed and held by (input::armsItem: the swim fists while the hands swim
//           with a gun held), "" for none; hstate the hands' state (idHands::destHandsState: 0 HS_IDLE,
//           12 HS_SWIM_IDLE, 14 HS_TRANSITIONING), or null; hswim whether the hands swim (true / false,
//           input::handsSwim), or null
//   mods    the held weapon's active mods: the decl names of its two mod overrides ["" for none, ...]
//           (vkcore/weapon_mods.hpp); sight the key of the [sights] row the viewmodel turns the weapon by, ""
//           for none

#include "features/arm/arm_joints.hpp"
#include "xr_math/weapon_pose.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace evr::arm {

struct DumpHandInput {
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
    float rollDegrees = 0.0f;
    std::optional<Vec3> position; // metres from the head, LOCAL
};

// The free off hand's fingers ("fingers"): the curls, and each finger bone's turn written and read (degrees).
struct DumpFingers {
    std::array<float, 5> curl{};
    std::array<float, 15> want{};
    std::optional<std::array<float, 15>> read;
};

struct DumpArm {
    bool layer = false;  // the layer posed the arm this tick (else the game did)
    float weight = 0.0f; // the layer's share
    float twistDegrees = 0.0f;
    float landed = -1.0f;
    Vec3 scale{1.0f, 1.0f, 1.0f};       // the animator's model scale read with the poses
    std::optional<ArmPoses> finalPoses; // "final"
    std::optional<ArmPoses> readPoses;  // "read"
    std::optional<ArmPoses> animated;   // "animated"
    std::optional<xr_math::ModelPose> target;
    std::optional<xr_math::ModelPose> root; // the skeleton's root (joint 0) as read with the poses
    std::uint32_t zeroScale = 0; // "zero": the joints written scaled to nothing (bit n: ArmJoint n)
    std::optional<DumpFingers> fingers;
};

struct ArmDumpRecord {
    double seconds = 0.0;
    float unitsPerMetre = 1.0f;
    Vec3 scale{1.0f, 1.0f, 1.0f};
    const char* fault = "none";
    std::array<std::optional<DumpHandInput>, 2> input; // left, right
    std::array<DumpArm, 2> arms;                       // left, right
    bool modelValid = false;
    Vec3 eye;
    xr_math::EyeRelativePose model;
    std::optional<xr_math::IdViewAxis> head;
    std::string item;
    std::optional<xr_math::ModelPose> weapon;
    bool aimValid = false;
    xr_math::EyeRelativePose aim; // offset = the ray's start from the eye, axis.forward = its direction
    std::optional<Vec3> grip;
    std::optional<Vec3> offGrip;
    std::string armsItem;
    std::optional<int> handsState;
    std::optional<bool> handsSwim;
    std::array<std::string, 2> mods;
    std::string sightRow;
};

// The joint roles in a pose set, in ArmJoint order.
inline constexpr std::array<const char*, kArmJointCount> kDumpJointKeys{
    "attach", "hand", "roll3", "roll2", "roll1", "forearmroll", "forearm", "upperarm"};

// The record as one line of JSON, ending in '\n'.
std::string armDumpLine(const ArmDumpRecord& record);

} // namespace evr::arm
