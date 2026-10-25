#include "features/arm/twist_fault.hpp"

#include "features/arm/arm_frames.hpp"

namespace evr::arm {

void plantTwistFault(ArmSolution& solution, const ArmPoses& animated) {
    ArmPoses& joints = solution.joints;
    constexpr ArmJoint kRolls[] = {ArmJoint::Roll3, ArmJoint::Roll2, ArmJoint::Roll1, ArmJoint::ForeArmRoll};
    if (solution.twistFrom == TwistFrom::Bind) {
        // Each roll joint takes the elbow joint's orientation: the share of the forearm's twist it took is
        // gone, and the roll joints stay on the forearm where they were.
        const IdViewAxis elbow = joints[index(ArmJoint::ForeArm)].axis;
        for (const ArmJoint joint : kRolls) {
            joints[index(joint)].axis = elbow;
        }
        return;
    }
    // Measured from the animated hand, each roll joint turned back by its share about the solved forearm.
    const Vec3 elbow = joints[index(ArmJoint::ForeArm)].position;
    const Vec3 forearm = joints[index(ArmJoint::Hand)].position - elbow;
    if (!(length(forearm) > 1e-6f)) {
        return;
    }
    const Vec3 along = normalize(forearm);
    const Vec3 elbowA = animated[index(ArmJoint::ForeArm)].position;
    const Vec3 wristA = animated[index(ArmJoint::Hand)].position;
    for (const ArmJoint joint : kRolls) {
        const float angle =
            -solution.split.forearm * twistShare(animated[index(joint)].position, elbowA, wristA);
        ModelPose& pose = joints[index(joint)];
        pose = {elbow + rotateAbout(pose.position - elbow, along, angle),
                rotateAbout(pose.axis, along, angle)};
    }
}

HandsOnlyArm plantForearmFault(const ArmPoses& arm) {
    HandsOnlyArm out{arm, 0};
    out.joints[index(ArmJoint::UpperArm)] = arm[index(ArmJoint::ForeArm)];
    out.zeroScale = 1u << index(ArmJoint::UpperArm);
    return out;
}

} // namespace evr::arm
