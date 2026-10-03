#pragma once

// A whole arm for a wrist target (docs/VR_HANDS_HUD.md, "The whole arm" and "The weapon arm"): every joint
// of one of the game's arms posed in the arms model's space, from the game's animated pose of this tick.
// The same for either arm: everything is taken from the arm's own animated geometry, and the one axis
// convention used (the bend normal of a straight animated arm) holds on both sides of the mirrored rig
// (arm_joints.hpp).
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
// The weapon arm (solveArmToWrist) keeps its wrist where the game animated it, under the gun, and moves
// the shoulder instead when the wrist is out of its reach.
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

// The weapon arm: the wrist stays exactly at its animated pose and the forearm, elbow and upper arm reach
// it from `shoulder`, bending toward `pole`. A shoulder further from the wrist than the arm reaches (or
// closer than it folds) is moved along the line to the wrist until the bones meet it
// (two_bone_ik.hpp, rootWithinReach), so the sleeve never parts from the glove. The attach joint is left
// as animated (the game's modifier places the gun). nullopt as for solveArm.
std::optional<ArmSolution> solveArmToWrist(const ArmPoses& animated, Vec3 shoulder, Vec3 pole);

// The arm as the game would place it: every joint moved rigidly with the attach joint from its animated
// pose to `attach` (the game's modifier moves only that joint; the rest follow as its children).
ArmPoses followAttach(const ArmPoses& animated, const xr_math::ModelPose& attach);

// The share of the wrist's twist a joint at `position` takes: 0 at `elbow`, 1 at `wrist`, by its
// projection on the forearm.
float twistShare(Vec3 position, Vec3 elbow, Vec3 wrist);

} // namespace evr::arm
