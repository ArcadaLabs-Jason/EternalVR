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
//   wrist           the target's position (or the clamped one) with the target's orientation, its swing
//                   away from the forearm limited (arm_twist.hpp; not for the weapon arm, whose wrist is
//                   the game's)
//   twist           the wrist's turn about the forearm away from the forearm's neutral frame (the bind
//                   pose's relation rebuilt on the solved forearm, arm_twist.hpp), unwrapped against the
//                   last tick's (`ArmTargets::lastTwist`) so it never jumps as it crosses half a turn
//   forearm, elbow  each roll joint keeps its animated offset along the forearm and takes the forearm's
//                   neutral frame turned by its share of the forearm's twist, by its distance from the
//                   elbow (none at the elbow, nearly all next to the wrist), so a turned palm twists the
//                   sleeve instead of the wrist; the elbow joint and the roll joints all turn further by
//                   the upper arm's share (the twist past the forearm's range)
//   upper arm       LeftArm in the upper arm's neutral frame, turned by the upper arm's share as far as its
//                   axis lines up with the forearm's (all of it for a straight arm, none at a right angle)
//   attach          lefthandattach placed so its animated offset to LeftHand lands the wrist on target
//                   (the game's own modifier of this joint is what moves it)
//
// The weapon arm (solveArmToWrist) keeps its wrist where the game animated it, under the gun, and moves
// the shoulder instead when the wrist is out of its reach.
//
// Pure: no game memory, tested on its own.

#include "features/arm/arm_joints.hpp"
#include "features/arm/arm_twist.hpp"
#include "features/arm/two_bone_ik.hpp"

#include <cstdint>
#include <optional>

namespace evr::arm {

// What the wrist's twist is measured from: the bind pose (arm_twist.hpp; ETERNALVR_ARM_SOLVE=bind, the
// layer's default) or, as the solve did before, the game's animated hand carried by the forearm, wrapped to
// half a turn every tick, with every joint keeping its animated relation to its bone and the wrist's swing
// not limited (ETERNALVR_ARM_SOLVE=animated).
enum class TwistFrom : std::uint8_t {
    Bind,
    Animated,
};

struct ArmTargets {
    xr_math::ModelPose hand; // the wrist (LeftHand) pose wanted
    Vec3 shoulder;           // where the upper arm starts
    Vec3 pole;               // the direction the elbow bends toward
    // The twist (ArmSolution::twist) of the last tick this arm was solved, so the twist stays continuous;
    // none for the first tick, which takes the branch within half a turn of the centre.
    std::optional<float> lastTwist;
    bool limitSwing = true; // the wrist's swing limited (the off hand); the weapon arm keeps the game's wrist
    // The solve as before when nothing is given; the layer passes the setting's (vkcore/arm_solve_mode.hpp,
    // bind by default).
    TwistFrom twistFrom = TwistFrom::Animated;
    // The wrist always on its target (handsOnlyTargets): a shoulder further from it than the arm reaches, or
    // closer than it folds, is moved along the line to it until the bones meet it (rootWithinReach).
    bool reachAlways = false;
};

struct ArmSolution {
    ArmPoses joints;
    TwoBoneSolution ik;
    float upper = 0.0f; // the bone lengths used
    float lower = 0.0f;
    float twist = 0.0f; // the wrist's twist from the bind pose (radians), unwrapped: pass back as lastTwist
    TwistSplit split;   // how it was shared out
    SwingResult swing;  // the wrist's swing asked for and whether it was limited
    TwistFrom twistFrom = TwistFrom::Animated;
};

// nullopt when an animated pose is implausible or a bone has no length.
std::optional<ArmSolution>
solveArm(const ArmPoses& animated, const ArmTargets& targets, const TwistLimits& limits = kTwistLimits);

// The weapon arm: the wrist stays exactly at its animated pose and the forearm, elbow and upper arm reach
// it from `shoulder`, bending toward `pole`. A shoulder further from the wrist than the arm reaches (or
// closer than it folds) is moved along the line to the wrist until the bones meet it
// (two_bone_ik.hpp, rootWithinReach), so the sleeve never parts from the glove. The attach joint is left
// as animated (the game's modifier places the gun). nullopt as for solveArm.
std::optional<ArmSolution> solveArmToWrist(const ArmPoses& animated,
                                           Vec3 shoulder,
                                           Vec3 pole,
                                           std::optional<float> lastTwist = std::nullopt,
                                           TwistFrom twistFrom = TwistFrom::Animated);

// The arm as the game would place it: every joint moved rigidly with the attach joint from its animated
// pose to `attach` (the game's modifier moves only that joint; the rest follow as its children).
ArmPoses followAttach(const ArmPoses& animated, const xr_math::ModelPose& attach);

// The off hand's targets for hands only (ETERNALVR_ARMS=hands): only the glove is drawn, so no forearm
// holds it short. The wrist reaches its target wherever it is (`reachAlways`), and its swing is limited as
// the shown arm's is (`limitSwing` as given), so the glove turns as the shown arm's glove does: in the
// owner's headset test of 2026-10-24 the shown arm's glove was right and the unlimited one looked turned
// about 90 degrees with the arm out and the palm down. The arm behind it is still solved, its twist kept
// continuous, and then folded into the wrist (handsOnly).
ArmTargets handsOnlyTargets(ArmTargets targets);

// The off hand's sanity limits on a tick's writes (game units): how far the wrist may move from the game's
// animated hand, and the attach joint's modifier reach. Shown arms keep `maxReach` and `maxAttach`: a wrist
// further away would stretch the arm, so the tick is given back to the game. Hands only draws the glove
// alone, so a far controller is no reason to give the tick back (that would draw the game's whole arm for
// it): its limits grow with the wrist's move. A non-finite move keeps the plain limits, so it is still
// rejected.
struct ReachLimits {
    float wrist = 0.0f;
    float attach = 0.0f;
};
ReachLimits reachLimits(bool handsOnly, float wristMove, float maxReach, float maxAttach);

// Hands only (ETERNALVR_ARMS=hands): the forearm's roll joints, the elbow and the shoulder put on
// the wrist (or the last roll joint), so the skin they carry, written with a zero scale (game_arm::writeArm),
// shrinks into it and the glove is left. The attach joint and the wrist stay. How it closes the sleeve at the
// glove (ETERNALVR_TEST_HANDS_CUFF, rig tests; `fold` for players):
enum class Cuff : std::uint8_t {
    Fold, // every joint from the last roll joint to the shoulder on the wrist, scaled to nothing
    Stub, // the last roll joint left where the arm has it (a stub of forearm about 6 cm long), the joints
          // after it on it, scaled to nothing
    Ring, // the forearm's roll joints on the wrist at full size (the sleeve's rings drawn flat over the
          // cuff), the elbow and the shoulder on it, scaled to nothing
};

// The arm for hands only with `cuff`, and the joints to write scaled to nothing (bit n for ArmJoint n).
struct HandsOnlyArm {
    ArmPoses joints;
    std::uint32_t zeroScale = 0;
};
HandsOnlyArm handsOnly(const ArmPoses& joints, Cuff cuff);

// The share of the wrist's twist a joint at `position` takes: 0 at `elbow`, 1 at `wrist`, by its
// projection on the forearm.
float twistShare(Vec3 position, Vec3 elbow, Vec3 wrist);

} // namespace evr::arm
