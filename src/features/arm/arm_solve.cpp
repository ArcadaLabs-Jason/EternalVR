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

std::optional<ArmSolution> solveArm(const ArmPoses& animated, const ArmTargets& targets) {
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
    const auto ik =
        solveTwoBone({targets.shoulder, targets.hand.position, out.upper, out.lower, targets.pole});
    if (!ik) {
        return std::nullopt;
    }
    out.ik = *ik;
    const Vec3 shoulder = targets.shoulder;
    const Vec3 elbow = ik->joint;
    const Vec3 wrist = ik->end;

    // The elbow's bend plane, animated and solved, with the normal cross(upper arm, forearm).
    Vec3 normalA = cross(elbowA - shoulderA, wristA - elbowA);
    if (!(length(normalA) > 1e-4f * out.upper * out.lower)) {
        // A straight animated arm: the forearm frame's -z is the bend normal in the rig [static, bind pose].
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

    // The wrist, and its twist about the forearm relative to the hand carried rigidly by the forearm.
    ModelPose& hand = at(out.joints, ArmJoint::Hand);
    hand = {wrist, targets.hand.axis};
    const ModelPose carried = compose(forearm, relative(forearmA, at(animated, ArmJoint::Hand)));
    const Vec3 along = forearm.axis.forward;
    const float twist = twistAbout(carried.axis, hand.axis, along);
    out.twist = twist;

    for (const ArmJoint joint : kForearmJoints) {
        const ModelPose base = compose(forearm, relative(forearmA, at(animated, joint)));
        const float angle = twist * twistShare(at(animated, joint).position, elbowA, wristA);
        at(out.joints, joint) = {elbow + rotateAbout(base.position - elbow, along, angle),
                                 rotateAbout(base.axis, along, angle)};
    }
    at(out.joints, ArmJoint::UpperArm) = compose(upper, relative(upperA, at(animated, ArmJoint::UpperArm)));
    at(out.joints, ArmJoint::Attach) =
        parentFor(hand, relative(at(animated, ArmJoint::Attach), at(animated, ArmJoint::Hand)));

    for (const ModelPose& pose : out.joints) {
        if (!plausiblePose(pose)) {
            return std::nullopt;
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
