#include "features/arm/twist_fault.hpp"

#include "features/arm/arm_fixture.hpp"
#include "features/arm/arm_frames.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>

using evr::Vec3;
using evr::arm::ArmJoint;
using evr::arm::ArmPoses;
using evr::arm::ArmTargets;
using evr::arm::index;
using evr::arm::plantTwistFault;
using evr::arm::rotateAbout;
using evr::arm::solveArm;
using evr::arm::twistAbout;
using evr::test::approxEqual;
using evr::test::approxPose;
using evr::test::bindArm;
using evr::xr_math::ModelPose;

namespace {

const ModelPose& at(const ArmPoses& poses, ArmJoint joint) {
    return poses[index(joint)];
}

// The pole that makes the IK put the elbow where the animation has it.
Vec3 animatedPole(const ArmPoses& a) {
    const Vec3 s = at(a, ArmJoint::UpperArm).position;
    const Vec3 dir = normalize(at(a, ArmJoint::Hand).position - s);
    const Vec3 e = at(a, ArmJoint::ForeArm).position - s;
    return e - dir * dot(e, dir);
}

constexpr ArmJoint kRolls[] = {ArmJoint::Roll3, ArmJoint::Roll2, ArmJoint::Roll1, ArmJoint::ForeArmRoll};

} // namespace

TEST_CASE("the planted twist fault leaves the whole wrist twist on the wrist joint") {
    const ArmPoses a = bindArm();
    const ModelPose& hand = at(a, ArmJoint::Hand);
    const Vec3 along = normalize(hand.position - at(a, ArmJoint::ForeArm).position);
    const float turn = 1.0f; // radians, about 57 degrees: within the forearm's range (arm_twist.hpp)
    const ModelPose turned{hand.position, rotateAbout(hand.axis, along, turn)};
    const Vec3 shoulder = at(a, ArmJoint::UpperArm).position;
    ArmTargets straightTargets{hand, shoulder, animatedPole(a)};
    ArmTargets twistedTargets{turned, shoulder, animatedPole(a)};
    straightTargets.twistFrom = evr::arm::TwistFrom::Bind;
    twistedTargets.twistFrom = evr::arm::TwistFrom::Bind;
    const auto straight = solveArm(a, straightTargets);
    auto twisted = solveArm(a, twistedTargets);
    REQUIRE(straight);
    REQUIRE(twisted);
    CHECK(approxEqual(twisted->twist, turn, 1e-3f));

    // Without the fault the roll joint next to the wrist turns with it, so the wrist itself turns little.
    const float shared = twistAbout(at(twisted->joints, ArmJoint::Roll3).axis,
                                    at(twisted->joints, ArmJoint::Hand).axis, along);
    CHECK(std::fabs(shared) < 0.5f * turn);

    plantTwistFault(*twisted, a);
    // Every roll joint is back where it is with no twist; the wrist, elbow and shoulder keep their poses.
    for (const ArmJoint j : kRolls) {
        CHECK(approxPose(at(twisted->joints, j), at(straight->joints, j), 1e-3f));
    }
    CHECK(approxPose(at(twisted->joints, ArmJoint::Hand), turned, 1e-3f));
    CHECK(approxPose(at(twisted->joints, ArmJoint::ForeArm), at(straight->joints, ArmJoint::ForeArm), 1e-3f));
    CHECK(
        approxPose(at(twisted->joints, ArmJoint::UpperArm), at(straight->joints, ArmJoint::UpperArm), 1e-3f));
    // The whole twist sits between the last roll joint and the wrist.
    const float wrung = twistAbout(at(twisted->joints, ArmJoint::Roll3).axis,
                                   at(twisted->joints, ArmJoint::Hand).axis, along);
    CHECK(approxEqual(wrung, turn, 1e-3f));
    // The roll joints keep their distances from the elbow (the bones are not stretched).
    for (const ArmJoint j : kRolls) {
        const Vec3 elbow = at(twisted->joints, ArmJoint::ForeArm).position;
        CHECK(approxEqual(length(at(twisted->joints, j).position - elbow),
                          length(at(a, j).position - at(a, ArmJoint::ForeArm).position), 1e-4f));
    }
}

TEST_CASE("the planted twist fault changes nothing without a twist") {
    const ArmPoses a = bindArm();
    const ArmTargets targets{at(a, ArmJoint::Hand), at(a, ArmJoint::UpperArm).position, animatedPole(a)};
    const auto solved = solveArm(a, targets);
    REQUIRE(solved);
    auto faulted = solved;
    plantTwistFault(*faulted, a);
    for (std::size_t i = 0; i < evr::arm::kArmJointCount; ++i) {
        CHECK(approxPose(faulted->joints[i], solved->joints[i], 1e-5f));
    }
}

TEST_CASE("measured from the animated hand the planted fault turns each roll joint back by its share") {
    const ArmPoses a = bindArm();
    const ModelPose& hand = at(a, ArmJoint::Hand);
    const Vec3 along = normalize(hand.position - at(a, ArmJoint::ForeArm).position);
    const float turn = 1.5f;
    const Vec3 shoulder = at(a, ArmJoint::UpperArm).position;
    ArmTargets straightTargets{hand, shoulder, animatedPole(a)};
    ArmTargets twistedTargets{
        {hand.position, rotateAbout(hand.axis, along, turn)}, shoulder, animatedPole(a)};
    straightTargets.twistFrom = evr::arm::TwistFrom::Animated;
    twistedTargets.twistFrom = evr::arm::TwistFrom::Animated;
    const auto straight = solveArm(a, straightTargets);
    auto twisted = solveArm(a, twistedTargets);
    REQUIRE(straight);
    REQUIRE(twisted);
    CHECK(approxEqual(twisted->twist, turn, 1e-3f));
    plantTwistFault(*twisted, a);
    for (const ArmJoint j : kRolls) {
        CHECK(approxPose(at(twisted->joints, j), at(straight->joints, j), 1e-3f));
    }
    CHECK(approxEqual(twistAbout(at(twisted->joints, ArmJoint::Roll3).axis,
                                 at(twisted->joints, ArmJoint::Hand).axis, along),
                      turn, 1e-3f));
}
