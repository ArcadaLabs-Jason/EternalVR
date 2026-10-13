#pragma once

// The hands' joint-modifier mix and its inverse (docs/VR_HANDS_HUD.md, "Where an override lands"): where
// the game puts an arm's joints for the modifiers written, and what to write so they land on target.
//
// The mix [static: InitJointMods 0x138B080 creates the hands' modifier node with the weight 0.95 (a
// constant passed at +0xB0), the node's create (0x497E00) copies it to the owner's current weight (+0x3C)
// and target weight (+0x40), GetWeight (0x497DE0) returns +0x3C; rig: the hands rig's dumps ap-base0 and
// ap-final, build 25216728]: the blend takes the arm's pose without modifiers (`animated`) and the pose with
// every modifier of the node applied whole (`modified`: an override puts its joint at the written pose, a
// change moves its joint from the animated pose, the children follow) and mixes them joint by joint in each
// joint's parent's frame at the node's weight, from the root down: offsets linearly, rotations by the
// quaternions mixed linearly the shortest way round and normalised (nlerp, not slerp: a turn of 90 degrees
// written lands 85.9 round, not 85.5; 150 lands 144.3, not 142.5) [static: the blend command 0x19EAE50,
// (1 - t) qA + t qB with the sign of qA.qB taken, renormalised with rsqrtps, translations lerped, in each
// joint's local frame; the modifiers are applied to a copy of the pose without them by 0x19E1040 in list
// order, each model-space one against its parent as modified so far; rig: round 4 below]. The weight covers
// every modifier of the node: the game's attach joints' and the layer's (rig run ap-w1: held at 1, every
// override landed exactly and the gun moved). So an override lands about 95 % of the way from the animated
// pose, and a change moves its joint about 95 % of the way.
//
// Round 4 [rig: r4-ref-bind, ap3-bind]: modelled as slerp, the first override below the wrist landed off by
// the difference between the two blends of the attach joint's turn (0.5 degrees at a 90 degree turn, 1.0 at
// 120, 1.9 at 150), since the attach joint lands on target through the read (preModifierPose) but the pose
// the game carries the wrist by is that difference away from the one the layer wrote against; the forearm
// after it was turned rigidly by as much, up to 19 mm at the shoulder.
//
// What GetJointTransforms reads is the final pose of the last frame, the game's modifiers included. A change
// computed from that read as if it were the animated pose (the off hand's attach joint until this step) is
// applied by the game to the pose without modifiers instead, which puts the joint between the two (about half
// way, and from a still start it swings about the target with a factor of -0.95 a frame): from that read the
// layer gets the pose without modifiers back with the change it wrote last (preModifierPose).
//
// The inverse writes each joint past its target (modifiedFor): offsets by 1 / weight, turns by the inverse of
// the blend's (a turn of g degrees written as 2 (g/2 + asin((1 - w) / w sin(g/2)))); the parent's frame is
// the target's own, since the parent lands on its target too.
//
// Pure: no game memory, tested on its own.

#include "features/arm/arm_joints.hpp"
#include "xr_math/offhand_pose.hpp"

#include <optional>

namespace evr::arm {

// A weight the inverse can use: above 0.5 (dividing by less would more than double what is written) and
// at most 1.
bool usableModWeight(float weight);

// `a` moved toward `b` by `t` (0 to 1) as the blend does: positions linearly, axes by nlerp the shortest way
// round.
xr_math::ModelPose mixPose(const xr_math::ModelPose& a, const xr_math::ModelPose& b, float t);

// The inverse at `weight`: what mixPose(a, ..., weight) takes to `b`, its turn kept short of half a turn
// (beyond about 173 degrees at 0.95 it lands short). `weight` must be usable.
xr_math::ModelPose pastPose(const xr_math::ModelPose& a, const xr_math::ModelPose& b, float weight);

// The arm's final poses for the attach joint and the wrist and every joint after them, from the poses
// without modifiers (`animated`), the poses the modifiers put them at taken whole (`modified`) and the
// weight. The attach joint's parent (the skeleton's origin) has no modifier. The forward model, for tests.
ArmPoses mixedArm(const ArmPoses& animated, const ArmPoses& modified, float weight);

// The inverse: the modified poses that make mixedArm land the attach joint and the layer's joints (the
// forearm's roll joints, the elbow and the shoulder) on `wanted`, whose wrist must be where the attach joint
// carries it (wanted's attach joint composed with the animated wrist's offset from it). A joint is never
// written turned half a turn or more from its animated pose (the mix would take it the other way): a joint
// that must turn further than the mix reaches (173 degrees at 0.95: 179 written) lands short, and the joints
// after it are placed from where it lands, so they still land on their targets. With `modifiedHand` the
// attach joint is not the layer's (the weapon arm): the wrist's modified pose is taken as given and the
// attach joint left as animated. `weight` must be usable. The weapon arm passes the wrist as read, an
// approximation: the read has the game's attach-joint change (the weapon lag) mixed in at the weight, the
// modified wrist has it whole, so its joints land off by (1 - weight) of that change (rig r4-bind2, with
// the blend modelled as nlerp: 1.4 mm median, 1.8 at most; the 1.5 and 4.4 of ap2-comp2 included the
// slerp model's own error).
ArmPoses modifiedFor(const ArmPoses& animated,
                     const ArmPoses& wanted,
                     float weight,
                     const xr_math::ModelPose* modifiedHand = nullptr);

// The off hand's arm with a change of the layer's own on the wrist (ETERNALVR_ARM_SOLVE=bind): the modified
// poses (`modified`, as modifiedFor's) and whether the wrist turns (`wristTurned`).
struct WristArm {
    ArmPoses modified{};
    bool wristTurned = false;
};

// modifiedFor for the off hand, its wrist (LeftHand) taking the rest of a turn the attach joint cannot make
// (past 173 degrees at 0.95, where modifiedFor lands it short) with a change of its own, applied like the
// game's attach-joint change (rotation on the left, translation added) after the attach joint carries it,
// and mixed in the attach joint's frame like every joint; written past its target as the forearm's joints
// are, so every turn up to half a turn lands. The wrist does not turn (`wristTurned` false, the poses
// modifiedFor's) when the attach joint reaches. `weight` must be usable.
WristArm modifiedWithWrist(const ArmPoses& animated, const ArmPoses& wanted, float weight);

// The wrist's change for `written` (game units, model space): what takes its pose carried by the written
// attach joint to its written pose; no change when the wrist does not turn. `written` comes from
// modifiedWithWrist with `animated` (and so a usable weight).
xr_math::JointMod wristChange(const ArmPoses& animated, const WristArm& written);

// The pose without modifiers of a joint whose change `mod` (game units) was mixed at `weight` into `read`.
xr_math::ModelPose
preModifierPose(const xr_math::ModelPose& read, const xr_math::JointMod& mod, float weight);

// The arm read (`read`, its attach joint's change `mod` of the last frame mixed in) as it is without
// modifiers: carried rigidly by the attach joint's pose without it.
ArmPoses preModifierArm(const ArmPoses& read, const xr_math::JointMod& mod, float weight);

// The attach joint's change last handed to the game and the hands it was handed for (opaque): only those
// hands' next read has it mixed in. Hands are told apart by address alone: a new level's hands allocated at
// the old address would take the old change for one tick (rig-only path, ETERNALVR_ARM_SOLVE=bind).
struct HandedMod {
    const void* hands = nullptr;
    xr_math::JointMod mod;
};

// `last`'s change when it was handed for `hands`, otherwise nullopt (other hands, or none).
std::optional<xr_math::JointMod> handedTo(const std::optional<HandedMod>& last, const void* hands);

// The game's arm as the blend shows it: carried from `pre` (no modifiers) by the attach joint's change `mod`
// (game units) mixed in at `weight`.
ArmPoses gameArmMixed(const ArmPoses& pre, const xr_math::JointMod& mod, float weight);

} // namespace evr::arm
