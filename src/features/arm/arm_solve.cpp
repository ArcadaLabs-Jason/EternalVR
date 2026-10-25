#include "features/arm/arm_solve.hpp"

#include "features/arm/arm_frames.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace evr::arm {

namespace {

// The forearm's joints, each placed relative to the forearm frame and twisted by its share.
constexpr ArmJoint kForearmJoints[] = {ArmJoint::Roll3, ArmJoint::Roll2, ArmJoint::Roll1,
                                       ArmJoint::ForeArmRoll, ArmJoint::ForeArm};

const ModelPose& at(const ArmPoses& poses, ArmJoint joint) {
    return poses[index(joint)];
}

ModelPose& at(ArmPoses& poses, ArmJoint joint) {
    return poses[index(joint)];
}

// The attach joint placed to carry the wrist as animated, and every joint checked.
std::optional<ArmSolution> finish(ArmSolution& out, const ArmPoses& animated) {
    at(out.joints, ArmJoint::Attach) =
        parentFor(at(out.joints, ArmJoint::Hand),
                  relative(at(animated, ArmJoint::Attach), at(animated, ArmJoint::Hand)));
    for (const ModelPose& pose : out.joints) {
        if (!plausiblePose(pose)) {
            return std::nullopt;
        }
    }
    return out;
}

} // namespace

float twistShare(Vec3 position, Vec3 elbow, Vec3 wrist) {
    const Vec3 forearm = wrist - elbow;
    const float lengthSquared = dot(forearm, forearm);
    if (!(lengthSquared > 1e-12f)) {
        return 0.0f;
    }
    const float share = dot(position - elbow, forearm) / lengthSquared;
    return std::isfinite(share) ? std::clamp(share, 0.0f, 1.0f) : 0.0f;
}

std::optional<ArmSolution>
solveArm(const ArmPoses& animated, const ArmTargets& targets, const TwistLimits& limits) {
    for (const ModelPose& pose : animated) {
        if (!plausiblePose(pose)) {
            return std::nullopt;
        }
    }
    if (!plausiblePose(targets.hand)) {
        return std::nullopt;
    }
    const Vec3 shoulderA = at(animated, ArmJoint::UpperArm).position;
    const Vec3 elbowA = at(animated, ArmJoint::ForeArm).position;
    const Vec3 wristA = at(animated, ArmJoint::Hand).position;

    ArmSolution out;
    out.upper = length(elbowA - shoulderA);
    out.lower = length(wristA - elbowA);
    const Vec3 shoulder = targets.reachAlways
                              ? rootWithinReach(targets.shoulder, targets.hand.position, out.upper, out.lower)
                              : targets.shoulder;
    const auto ik = solveTwoBone({shoulder, targets.hand.position, out.upper, out.lower, targets.pole});
    if (!ik) {
        return std::nullopt;
    }
    out.ik = *ik;
    const Vec3 elbow = ik->joint;
    const Vec3 wrist = ik->end;

    // The elbow's bend plane, animated and solved, with the normal cross(upper arm, forearm).
    Vec3 normalA = cross(elbowA - shoulderA, wristA - elbowA);
    if (!(length(normalA) > 1e-4f * out.upper * out.lower)) {
        // A straight animated arm: the forearm frame's -z is the bend normal in the rig [static, bind pose].
        // The same on the right arm: its axes are the left's reflected and negated, and so is the normal
        // of its reflected bones.
        normalA = -at(animated, ArmJoint::ForeArm).axis.up;
    }
    const Vec3 normal = cross(ik->bend, normalize(wrist - shoulder));

    ModelPose forearmA{elbowA, {}};
    ModelPose upperA{elbowA, {}};
    ModelPose forearm{elbow, {}};
    ModelPose upper{elbow, {}};
    if (!frameFrom(wristA - elbowA, normalA, forearmA.axis) ||
        !frameFrom(elbowA - shoulderA, normalA, upperA.axis) ||
        !frameFrom(wrist - elbow, normal, forearm.axis) || !frameFrom(elbow - shoulder, normal, upper.axis)) {
        return std::nullopt;
    }

    ModelPose& hand = at(out.joints, ArmJoint::Hand);
    out.twistFrom = targets.twistFrom;
    if (targets.twistFrom == TwistFrom::Animated) {
        // The solve as it was (ETERNALVR_ARM_SOLVE=animated): the twist relative to the animated hand
        // carried by the forearm, wrapped to half a turn each tick; the joints keep their animated relation
        // to their bone; the wrist takes the target as it is.
        hand = {wrist, targets.hand.axis};
        const ModelPose carriedHand = compose(forearm, relative(forearmA, at(animated, ArmJoint::Hand)));
        const Vec3 along = forearm.axis.forward;
        out.twist = twistAbout(carriedHand.axis, hand.axis, along);
        out.split = {out.twist, 0.0f, 0.0f};
        out.swing = {hand.axis, 0.0f, 0.0f, false};
        for (const ArmJoint joint : kForearmJoints) {
            const ModelPose base = compose(forearm, relative(forearmA, at(animated, joint)));
            const float angle = out.twist * twistShare(at(animated, joint).position, elbowA, wristA);
            at(out.joints, joint) = {elbow + rotateAbout(base.position - elbow, along, angle),
                                     rotateAbout(base.axis, along, angle)};
        }
        at(out.joints, ArmJoint::UpperArm) =
            compose(upper, relative(upperA, at(animated, ArmJoint::UpperArm)));
        return finish(out, animated);
    }

    // The bones' neutral frames (arm_twist.hpp): the roll joints' y runs along the forearm, toward the
    // wrist on one side of the mirrored rig and toward the elbow on the other.
    const float side = dot(at(animated, ArmJoint::Roll3).axis.left, wristA - elbowA) >= 0.0f ? 1.0f : -1.0f;
    IdViewAxis forearmNeutral;
    IdViewAxis upperNeutral;
    if (!neutralFrame(wrist - elbow, normal, side, forearmNeutral) ||
        !neutralFrame(elbow - shoulder, normal, side, upperNeutral)) {
        return std::nullopt;
    }

    // The wrist: its swing limited, its twist from the forearm's neutral frame unwrapped against the last.
    out.swing = targets.limitSwing ? limitSwing(forearmNeutral, targets.hand.axis, limits)
                                   : SwingResult{targets.hand.axis, 0.0f, 0.0f, false};
    hand = {wrist, out.swing.hand};
    const Vec3 along = forearmNeutral.left;
    out.twist = unwrapTwist(twistAbout(forearmNeutral, hand.axis, along), targets.lastTwist, limits);
    out.split = splitTwist(out.twist, limits);

    for (const ArmJoint joint : kForearmJoints) {
        const ModelPose base = compose(forearm, relative(forearmA, at(animated, joint)));
        const float angle =
            out.split.upper + out.split.forearm * twistShare(at(animated, joint).position, elbowA, wristA);
        at(out.joints, joint) = {elbow + rotateAbout(base.position - elbow, along, angle),
                                 rotateAbout(forearmNeutral, along, angle)};
    }
    // The upper arm carries the elbow's turn as far as its axis lines up with the forearm's.
    const float carried = out.split.upper * std::clamp(dot(upperNeutral.left, along), 0.0f, 1.0f);
    at(out.joints,
       ArmJoint::UpperArm) = {compose(upper, relative(upperA, at(animated, ArmJoint::UpperArm))).position,
                              rotateAbout(upperNeutral, upperNeutral.left, carried)};
    return finish(out, animated);
}

std::optional<ArmSolution> solveArmToWrist(
    const ArmPoses& animated, Vec3 shoulder, Vec3 pole, std::optional<float> lastTwist, TwistFrom twistFrom) {
    const ModelPose& hand = at(animated, ArmJoint::Hand);
    const float upper =
        length(at(animated, ArmJoint::ForeArm).position - at(animated, ArmJoint::UpperArm).position);
    const float lower = length(hand.position - at(animated, ArmJoint::ForeArm).position);
    const ArmTargets targets{
        hand, rootWithinReach(shoulder, hand.position, upper, lower), pole, lastTwist, false, twistFrom};
    auto out = solveArm(animated, targets);
    if (!out || out->ik.clamped) {
        return std::nullopt;
    }
    // The IK lands on the wrist to rounding; the wrist is the game's, exactly.
    at(out->joints, ArmJoint::Hand) = hand;
    at(out->joints, ArmJoint::Attach) = at(animated, ArmJoint::Attach);
    return out;
}

ArmTargets handsOnlyTargets(ArmTargets targets) {
    targets.reachAlways = true;
    return targets;
}

ReachLimits reachLimits(bool handsOnly, float wristMove, float maxReach, float maxAttach) {
    if (!handsOnly || !std::isfinite(wristMove) || wristMove <= maxReach) {
        return {maxReach, maxAttach};
    }
    return {wristMove, maxAttach + (wristMove - maxReach)};
}

HandsOnlyArm handsOnly(const ArmPoses& joints, Cuff cuff) {
    constexpr ArmJoint kFolded[] = {ArmJoint::Roll3,       ArmJoint::Roll2,   ArmJoint::Roll1,
                                    ArmJoint::ForeArmRoll, ArmJoint::ForeArm, ArmJoint::UpperArm};
    HandsOnlyArm out{joints, 0};
    const ModelPose onto = cuff == Cuff::Stub ? at(joints, ArmJoint::Roll3) : at(joints, ArmJoint::Hand);
    for (const ArmJoint joint : kFolded) {
        if (cuff == Cuff::Stub && joint == ArmJoint::Roll3) {
            continue;
        }
        at(out.joints, joint) = onto;
        const bool roll = joint != ArmJoint::ForeArm && joint != ArmJoint::UpperArm;
        if (cuff != Cuff::Ring || !roll) {
            out.zeroScale |= 1u << index(joint);
        }
    }
    return out;
}

ArmPoses followAttach(const ArmPoses& animated, const ModelPose& attach) {
    ArmPoses out;
    const ModelPose& from = at(animated, ArmJoint::Attach);
    for (std::size_t i = 0; i < kArmJointCount; ++i) {
        out[i] = compose(attach, relative(from, animated[i]));
    }
    return out;
}

} // namespace evr::arm
