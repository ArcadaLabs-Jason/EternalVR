#pragma once

// The whole off-hand arm for a wrist target (docs/VR_HANDS_HUD.md, "Off hand"): every joint of the
// game's left arm posed in the arms model's space, from the game's animated pose of this tick.
//
//   bone lengths    the animated shoulder-elbow and elbow-wrist distances (LeftArm, LeftForeArm, LeftHand)
//   elbow           two-bone IK from the shoulder to the wrist target, bending toward the pole
//                   (two_bone_ik.hpp); out of reach, the wrist stops short on the line to the target
//   wrist           the target's position (or the clamped one) with the target's orientation
//   forearm, elbow  each roll joint and the elbow keep their animated offset and orientation relative to
//                   the forearm (a frame along elbow -> wrist, turned about it by the elbow's bend plane),
//                   so the rig's own conventions carry over; the rolls then take their share of the
//                   wrist's twist about the forearm, by their distance from the elbow (none at the elbow,
//                   nearly all next to the wrist), so a turned palm twists the sleeve instead of the wrist
//   upper arm       LeftArm keeps its animated orientation relative to the upper arm's frame
//   attach          lefthandattach placed so its animated offset to LeftHand lands the wrist on target
//                   (the game's own modifier of this joint is what moves it)
//
// Pure: no game memory, tested on its own.

#include "features/arm/arm_joints.hpp"
#include "features/arm/two_bone_ik.hpp"

#include <optional>

namespace evr::arm {

struct ArmTargets {
    xr_math::ModelPose hand; // the wrist (LeftHand) pose wanted
    Vec3 shoulder;           // where the upper arm starts
    Vec3 pole;               // the direction the elbow bends toward
};

struct ArmSolution {
    ArmPoses joints;
    TwoBoneSolution ik;
    float upper = 0.0f; // the bone lengths used
    float lower = 0.0f;
    float twist = 0.0f; // the wrist's twist about the forearm (radians) the roll joints share
};

// nullopt when an animated pose is implausible or a bone has no length.
std::optional<ArmSolution> solveArm(const ArmPoses& animated, const ArmTargets& targets);

// The arm as the game would place it: every joint moved rigidly with the attach joint from its animated
// pose to `attach` (the game's modifier moves only that joint; the rest follow as its children).
ArmPoses followAttach(const ArmPoses& animated, const xr_math::ModelPose& attach);

// The share of the wrist's twist a joint at `position` takes: 0 at `elbow`, 1 at `wrist`, by its
// projection on the forearm.
float twistShare(Vec3 position, Vec3 elbow, Vec3 wrist);

} // namespace evr::arm
