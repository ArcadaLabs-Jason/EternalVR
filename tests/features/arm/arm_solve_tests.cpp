#include "features/arm/arm_solve.hpp"

#include "features/arm/arm_fixture.hpp"
#include "features/arm/arm_frames.hpp"
#include "xr_math/head_aim.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <numbers>

using evr::Vec3;
using evr::arm::ArmJoint;
using evr::arm::ArmPoses;
using evr::arm::ArmTargets;
using evr::arm::followAttach;
using evr::arm::index;
using evr::arm::kArmJointCount;
using evr::arm::kMaxReachFraction;
using evr::arm::relative;
using evr::arm::rotateAbout;
using evr::arm::solveArm;
using evr::arm::twistAbout;
using evr::arm::twistShare;
using evr::test::approxAxis;
using evr::test::approxEqual;
using evr::test::approxPose;
using evr::test::bindArm;
using evr::test::bindRightArm;
using evr::test::mirrored;
using evr::xr_math::ModelPose;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

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

float upperLength(const ArmPoses& a) {
    return length(at(a, ArmJoint::ForeArm).position - at(a, ArmJoint::UpperArm).position);
}

float lowerLength(const ArmPoses& a) {
    return length(at(a, ArmJoint::Hand).position - at(a, ArmJoint::ForeArm).position);
}

void checkBones(const ArmPoses& solved, const ArmPoses& animated) {
    CHECK(approxEqual(upperLength(solved), upperLength(animated)));
    CHECK(approxEqual(lowerLength(solved), lowerLength(animated)));
    // The roll joints stay on the forearm, spaced as animated.
    for (const ArmJoint j : {ArmJoint::Roll3, ArmJoint::Roll2, ArmJoint::Roll1, ArmJoint::ForeArmRoll}) {
        CHECK(approxEqual(length(at(solved, j).position - at(solved, ArmJoint::ForeArm).position),
                          length(at(animated, j).position - at(animated, ArmJoint::ForeArm).position)));
    }
    // The attach joint carries the wrist as the animation does.
    CHECK(approxPose(relative(at(solved, ArmJoint::Attach), at(solved, ArmJoint::Hand)),
                     relative(at(animated, ArmJoint::Attach), at(animated, ArmJoint::Hand))));
}

} // namespace

TEST_CASE("the animated arm solves to itself") {
    const ArmPoses a = bindArm();
    const ArmTargets targets{at(a, ArmJoint::Hand), at(a, ArmJoint::UpperArm).position, animatedPole(a)};
    const auto s = solveArm(a, targets);
    REQUIRE(s);
    CHECK_FALSE(s->ik.clamped);
    CHECK(approxEqual(s->upper, 0.28275f, 1e-4f));
    CHECK(approxEqual(s->lower, 0.2808f, 1e-3f));
    CHECK(approxEqual(s->twist, 0.0f));
    for (std::size_t i = 0; i < kArmJointCount; ++i) {
        CHECK(approxPose(s->joints[i], a[i], 2e-4f));
    }
}

TEST_CASE("a wrist target within reach is met with the target's orientation") {
    const ArmPoses a = bindArm();
    const Vec3 shoulder = at(a, ArmJoint::UpperArm).position;
    ArmTargets targets;
    targets.shoulder = shoulder;
    targets.hand = {shoulder + Vec3{0.35f, 0.05f, -0.2f},
                    evr::xr_math::axisFromAngles({30.0f, 20.0f, -60.0f})};
    targets.pole = {-0.2f, 0.6f, -1.0f};
    const auto s = solveArm(a, targets);
    REQUIRE(s);
    CHECK_FALSE(s->ik.clamped);
    CHECK(approxPose(at(s->joints, ArmJoint::Hand), targets.hand));
    CHECK(approxEqual(at(s->joints, ArmJoint::UpperArm).position, shoulder));
    checkBones(s->joints, a);
    // The elbow bends down and out.
    const Vec3 elbow = at(s->joints, ArmJoint::ForeArm).position;
    const Vec3 mid = shoulder + (targets.hand.position - shoulder) * 0.5f;
    CHECK(dot(elbow - mid, targets.pole) > 0.0f);
}

TEST_CASE("a wrist target beyond reach stops at the arm's length on the way to it") {
    const ArmPoses a = bindArm();
    const Vec3 shoulder = at(a, ArmJoint::UpperArm).position;
    const Vec3 far = shoulder + Vec3{1.2f, 0.3f, 0.1f};
    const ArmTargets targets{{far, at(a, ArmJoint::Hand).axis}, shoulder, {0.0f, 0.5f, -1.0f}};
    const auto s = solveArm(a, targets);
    REQUIRE(s);
    CHECK(s->ik.clamped);
    CHECK(s->ik.reach > 2.0f);
    const Vec3 wrist = at(s->joints, ArmJoint::Hand).position;
    CHECK(approxEqual(length(wrist - shoulder), (s->upper + s->lower) * kMaxReachFraction));
    CHECK(approxEqual(normalize(wrist - shoulder), normalize(far - shoulder)));
    checkBones(s->joints, a);
}

TEST_CASE("a turned palm twists the forearm by each roll joint's share") {
    const ArmPoses a = bindArm();
    const Vec3 along = normalize(at(a, ArmJoint::Hand).position - at(a, ArmJoint::ForeArm).position);
    ArmTargets targets{at(a, ArmJoint::Hand), at(a, ArmJoint::UpperArm).position, animatedPole(a)};
    const float turn = kPi / 3.0f;
    targets.hand.axis = rotateAbout(targets.hand.axis, along, turn);
    const auto s = solveArm(a, targets);
    REQUIRE(s);
    CHECK(approxEqual(s->twist, turn, 1e-3f));
    CHECK(approxAxis(at(s->joints, ArmJoint::Hand).axis, targets.hand.axis));
    // The elbow does not turn; the roll next to the wrist nearly all the way.
    CHECK(approxAxis(at(s->joints, ArmJoint::ForeArm).axis, at(a, ArmJoint::ForeArm).axis, 2e-4f));
    for (const ArmJoint j : {ArmJoint::Roll3, ArmJoint::Roll2, ArmJoint::Roll1, ArmJoint::ForeArmRoll}) {
        const float share =
            twistShare(at(a, j).position, at(a, ArmJoint::ForeArm).position, at(a, ArmJoint::Hand).position);
        CHECK(approxEqual(twistAbout(at(a, j).axis, at(s->joints, j).axis, along), turn * share, 1e-3f));
    }
    CHECK(approxEqual(twistShare(at(a, ArmJoint::Roll3).position, at(a, ArmJoint::ForeArm).position,
                                 at(a, ArmJoint::Hand).position),
                      0.779f, 2e-3f));
    checkBones(s->joints, a);
}

TEST_CASE("the twist share runs from the elbow to the wrist") {
    const Vec3 elbow{0.0f, 0.0f, 0.0f};
    const Vec3 wrist{0.0f, 0.3f, 0.0f};
    CHECK(approxEqual(twistShare(elbow, elbow, wrist), 0.0f));
    CHECK(approxEqual(twistShare({0.1f, 0.15f, 0.0f}, elbow, wrist), 0.5f));
    CHECK(approxEqual(twistShare({0.0f, 0.5f, 0.0f}, elbow, wrist), 1.0f));
    CHECK(approxEqual(twistShare({0.0f, 0.1f, 0.0f}, elbow, elbow), 0.0f));
}

TEST_CASE("an unusable animated pose or target is refused") {
    ArmPoses a = bindArm();
    const ArmTargets targets{at(a, ArmJoint::Hand), at(a, ArmJoint::UpperArm).position, {0.0f, 0.0f, -1.0f}};
    ArmPoses broken = a;
    broken[index(ArmJoint::Roll2)].axis.up = {0.0f, 0.0f, 0.0f};
    CHECK_FALSE(solveArm(broken, targets));
    broken = a;
    broken[index(ArmJoint::ForeArm)].position = at(a, ArmJoint::UpperArm).position; // no upper arm
    CHECK_FALSE(solveArm(broken, targets));
    ArmTargets badTarget = targets;
    badTarget.hand.axis.forward = {2.0f, 0.0f, 0.0f};
    CHECK_FALSE(solveArm(a, badTarget));
}

TEST_CASE("the game's arm follows its attach joint rigidly") {
    const ArmPoses a = bindArm();
    const ArmPoses same = followAttach(a, at(a, ArmJoint::Attach));
    for (std::size_t i = 0; i < kArmJointCount; ++i) {
        CHECK(approxPose(same[i], a[i]));
    }
    const Vec3 shift{0.1f, -0.05f, 0.02f};
    ModelPose moved = at(a, ArmJoint::Attach);
    moved.position = moved.position + shift;
    const ArmPoses shifted = followAttach(a, moved);
    for (std::size_t i = 0; i < kArmJointCount; ++i) {
        CHECK(approxEqual(shifted[i].position, a[i].position + shift));
        CHECK(approxAxis(shifted[i].axis, a[i].axis));
    }
}

TEST_CASE("the right arm, mirrored in the rig, solves to itself") {
    const ArmPoses a = bindRightArm();
    // Every joint but the attach joint, which is unturned on both sides.
    for (std::size_t i = 1; i < kArmJointCount; ++i) {
        CHECK(approxPose(a[i], mirrored(bindArm()[i]), 1e-5f));
    }
    const ArmTargets targets{at(a, ArmJoint::Hand), at(a, ArmJoint::UpperArm).position, animatedPole(a)};
    const auto s = solveArm(a, targets);
    REQUIRE(s);
    CHECK(approxEqual(s->twist, 0.0f));
    for (std::size_t i = 0; i < kArmJointCount; ++i) {
        CHECK(approxPose(s->joints[i], a[i], 2e-4f));
    }
}

TEST_CASE("the right arm solves as the mirror image of the left") {
    const ArmPoses left = bindArm();
    const ArmPoses right = bindRightArm();
    const Vec3 shoulder = at(left, ArmJoint::UpperArm).position;
    ArmTargets l;
    l.shoulder = shoulder + Vec3{0.02f, -0.05f, 0.03f};
    l.hand = {shoulder + Vec3{0.3f, 0.1f, -0.25f},
              rotateAbout(at(left, ArmJoint::Hand).axis, normalize(Vec3{0.2f, 0.5f, 0.3f}), 0.6f)};
    l.pole = {-0.2f, 0.6f, -1.0f};
    const ArmTargets r{
        mirrored(l.hand), {l.shoulder.x, -l.shoulder.y, l.shoulder.z}, {l.pole.x, -l.pole.y, l.pole.z}};
    const auto sl = solveArm(left, l);
    const auto sr = solveArm(right, r);
    REQUIRE(sl);
    REQUIRE(sr);
    CHECK(std::fabs(sl->twist) > 0.1f);
    CHECK(approxEqual(sr->twist, -sl->twist, 1e-3f));
    // Every joint the arm moves itself (the attach joint, unturned on both sides, is placed to carry it).
    for (std::size_t i = 1; i < kArmJointCount; ++i) {
        CAPTURE(i);
        CHECK(approxPose(sr->joints[i], mirrored(sl->joints[i]), 1e-3f));
    }
}

TEST_CASE("the weapon arm keeps its wrist and reaches it from a shoulder at the head") {
    const ArmPoses a = bindRightArm();
    const ModelPose& hand = at(a, ArmJoint::Hand);
    // A shoulder beside and above the wrist, within reach, and the elbow bending down and out to the right.
    const Vec3 shoulder = hand.position + Vec3{-0.3f, -0.15f, 0.2f};
    const Vec3 pole{-0.2f, -0.6f, -1.0f};
    const auto s = evr::arm::solveArmToWrist(a, shoulder, pole);
    REQUIRE(s);
    CHECK_FALSE(s->ik.clamped);
    // The wrist and the attach joint are the game's, exactly.
    CHECK(at(s->joints, ArmJoint::Hand).position == hand.position);
    CHECK(at(s->joints, ArmJoint::Hand).axis.forward == hand.axis.forward);
    CHECK(at(s->joints, ArmJoint::Attach).position == at(a, ArmJoint::Attach).position);
    CHECK(approxEqual(at(s->joints, ArmJoint::UpperArm).position, shoulder));
    checkBones(s->joints, a);
    const Vec3 elbow = at(s->joints, ArmJoint::ForeArm).position;
    const Vec3 mid = shoulder + (hand.position - shoulder) * 0.5f;
    CHECK(dot(elbow - mid, pole) > 0.0f);
    // The roll joints stay on the line from the elbow to the wrist, as in the bind pose.
    for (const ArmJoint j : {ArmJoint::Roll3, ArmJoint::Roll2, ArmJoint::Roll1, ArmJoint::ForeArmRoll}) {
        const Vec3 along = normalize(hand.position - elbow);
        const Vec3 off = at(s->joints, j).position - elbow;
        CHECK(length(off - along * dot(off, along)) < 1e-3f);
    }
}

TEST_CASE("a weapon arm's shoulder out of reach is pulled toward the wrist, which stays") {
    const ArmPoses a = bindRightArm();
    const ModelPose& hand = at(a, ArmJoint::Hand);
    const float full = upperLength(a) + lowerLength(a);
    const Vec3 far = hand.position + Vec3{-0.9f, -0.3f, 0.4f};
    const auto s = evr::arm::solveArmToWrist(a, far, {0.0f, -0.6f, -1.0f});
    REQUIRE(s);
    CHECK_FALSE(s->ik.clamped);
    CHECK(at(s->joints, ArmJoint::Hand).position == hand.position);
    const Vec3 shoulder = at(s->joints, ArmJoint::UpperArm).position;
    CHECK(length(shoulder - hand.position) < full * kMaxReachFraction);
    CHECK(length(shoulder - hand.position) > full * 0.99f);
    CHECK(approxEqual(normalize(shoulder - hand.position), normalize(far - hand.position)));
    checkBones(s->joints, a);
    // On the wrist itself: too close to fold to, pushed out the same way.
    const Vec3 near = hand.position + Vec3{-0.001f, 0.0f, 0.0f};
    const auto n = evr::arm::solveArmToWrist(a, near, {0.0f, -0.6f, -1.0f});
    REQUIRE(n);
    CHECK(at(n->joints, ArmJoint::Hand).position == hand.position);
    CHECK(length(at(n->joints, ArmJoint::UpperArm).position - hand.position) > full * 0.04f);
    checkBones(n->joints, a);
}
