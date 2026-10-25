// Hands only (ETERNALVR_ARMS=hands, docs/VR_HANDS_HUD.md "Hands only"): the off-hand glove at its controller
// wherever it is, turned as the shown arm's glove (arm::handsOnlyTargets), the arm behind it folded into the
// wrist (arm::handsOnly), and the hands rig's planted fault that leaves the forearm drawn
// (arm::plantForearmFault).

#include "features/arm/arm_solve.hpp"

#include "features/arm/arm_fixture.hpp"
#include "features/arm/arm_frames.hpp"
#include "features/arm/twist_fault.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <numbers>
#include <string_view>
#include <vector>

using evr::Vec3;
using evr::arm::ArmJoint;
using evr::arm::ArmPoses;
using evr::arm::ArmTargets;
using evr::arm::handsOnlyTargets;
using evr::arm::index;
using evr::arm::relative;
using evr::arm::rotateAbout;
using evr::arm::solveArm;
using evr::arm::TwistFrom;
using evr::test::approxEqual;
using evr::test::approxPose;
using evr::test::bindArm;
using evr::xr_math::ModelPose;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr TwistFrom kSolves[] = {TwistFrom::Animated, TwistFrom::Bind};

const ModelPose& at(const ArmPoses& poses, ArmJoint joint) {
    return poses[index(joint)];
}

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

// Wrist targets a real hand reaches and the arm's bones do not: far ahead of the shoulder (a two-handed hold
// out in front), next to it, and in reach but bent back 100 degrees and rolled half a turn (past the off
// hand's swing limit, 70 by 35 degrees).
struct Case {
    const char* name;
    ModelPose target;
};

std::vector<Case> cases(const ArmPoses& a) {
    const ModelPose& hand = at(a, ArmJoint::Hand);
    const Vec3 shoulder = at(a, ArmJoint::UpperArm).position;
    const float full = upperLength(a) + lowerLength(a);
    const Vec3 out = normalize(hand.position - shoulder);
    const Vec3 forearm = normalize(hand.position - at(a, ArmJoint::ForeArm).position);
    const Vec3 across = normalize(cross(forearm, Vec3{0.0f, 0.0f, 1.0f}));
    const ModelPose bent{hand.position, rotateAbout(rotateAbout(hand.axis, across, 100.0f * kPi / 180.0f),
                                                    forearm, kPi * 0.9f)};
    return {
        {"far ahead", {shoulder + out * (full * 1.8f), hand.axis}},
        {"far and bent", {shoulder + out * (full * 1.5f) + Vec3{0.0f, 0.0f, -0.1f}, bent.axis}},
        {"at the shoulder", {shoulder + out * (full * 0.01f), hand.axis}},
        {"in reach, bent back and rolled", bent},
    };
}

} // namespace

TEST_CASE("hands only: the off-hand glove is at its target wherever it is, turned as the shown arm's glove") {
    const ArmPoses a = bindArm();
    const Vec3 shoulder = at(a, ArmJoint::UpperArm).position;
    for (const TwistFrom from : kSolves) {
        for (const Case& c : cases(a)) {
            CAPTURE(c.name);
            CAPTURE(static_cast<int>(from));
            ArmTargets targets{c.target, shoulder, animatedPole(a)};
            targets.twistFrom = from;
            targets.limitSwing = from == TwistFrom::Bind; // as the layer's solve modes have it
            const auto s = solveArm(a, handsOnlyTargets(targets));
            REQUIRE(s);
            CHECK(approxEqual(at(s->joints, ArmJoint::Hand).position, c.target.position, 1e-4f));
            // In reach, the glove turns as the shown arm's glove for the same target: the same swing limit.
            if (std::string_view(c.name) == "in reach, bent back and rolled") {
                const auto shown = solveArm(a, targets);
                REQUIRE(shown);
                CHECK(approxPose(at(s->joints, ArmJoint::Hand), at(shown->joints, ArmJoint::Hand), 1e-4f));
            }
            // The glove's attach joint carries the wrist as the animation does, and the arm behind it keeps
            // its bones (it is folded into the wrist only after the solve).
            CHECK(approxPose(relative(at(s->joints, ArmJoint::Attach), at(s->joints, ArmJoint::Hand)),
                             relative(at(a, ArmJoint::Attach), at(a, ArmJoint::Hand)), 1e-4f));
            CHECK(approxEqual(upperLength(s->joints), upperLength(a), 1e-4f));
            CHECK(approxEqual(lowerLength(s->joints), lowerLength(a), 1e-4f));
        }
    }
}

TEST_CASE("the arm's own targets stop the glove short of a far target, which hands only does not") {
    // Why hands only has targets of its own: with the forearm drawn the off hand's wrist stops on the line to
    // a target out of reach, so the glove would sit away from the controller.
    const ArmPoses a = bindArm();
    const Vec3 shoulder = at(a, ArmJoint::UpperArm).position;
    const Case far = cases(a)[0];
    ArmTargets targets{far.target, shoulder, animatedPole(a)};
    targets.twistFrom = TwistFrom::Bind;
    const auto s = solveArm(a, targets);
    REQUIRE(s);
    CHECK_FALSE(approxEqual(at(s->joints, ArmJoint::Hand).position, far.target.position, 1e-2f));
    const auto h = solveArm(a, handsOnlyTargets(targets));
    REQUIRE(h);
    CHECK(approxEqual(at(h->joints, ArmJoint::Hand).position, far.target.position, 1e-4f));
}

TEST_CASE("hands only's targets: the wrist reaches freely, the rest of the targets as given") {
    ArmTargets t;
    t.hand = {{1.0f, 2.0f, 3.0f}, {}};
    t.shoulder = {0.5f, 0.0f, 1.0f};
    t.pole = {0.0f, 1.0f, 0.0f};
    t.lastTwist = 0.25f;
    t.twistFrom = TwistFrom::Bind;
    CHECK(t.limitSwing);
    CHECK_FALSE(t.reachAlways);
    const ArmTargets h = handsOnlyTargets(t);
    CHECK(h.reachAlways);
    CHECK(h.limitSwing); // as given: the shown arm's swing limit
    CHECK(approxPose(h.hand, t.hand));
    CHECK(h.shoulder == t.shoulder);
    CHECK(h.pole == t.pole);
    CHECK(h.lastTwist == t.lastTwist);
    CHECK(h.twistFrom == t.twistFrom);
}

TEST_CASE(
    "hands only gives no tick back for a far controller; shown arms and a non-finite move keep the limits") {
    using evr::arm::reachLimits;
    const float reach = 1.5f;
    const float attach = 3.0f;
    // Shown arms: the plain limits however far the wrist moved.
    for (const float move : {0.2f, 1.5f, 4.0f}) {
        const auto shown = reachLimits(false, move, reach, attach);
        CHECK(shown.wrist == reach);
        CHECK(shown.attach == attach);
    }
    // Hands only within reach: the same.
    const auto near = reachLimits(true, 0.8f, reach, attach);
    CHECK(near.wrist == reach);
    CHECK(near.attach == attach);
    // Hands only, 4 m: the wrist passes and the attach joint keeps its margin over the wrist.
    const auto far = reachLimits(true, 4.0f, reach, attach);
    CHECK(4.0f <= far.wrist);
    CHECK(far.attach == doctest::Approx(attach + 2.5f));
    // A non-finite move is still rejected by `!(move <= wrist)`.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK_FALSE(nan <= reachLimits(true, nan, reach, attach).wrist);
    CHECK_FALSE(inf <= reachLimits(true, inf, reach, attach).wrist);
}

TEST_CASE("the planted forearm fault leaves the forearm drawn and folds only the upper arm") {
    const ArmPoses a = bindArm();
    const auto bit = [](ArmJoint j) {
        return std::uint32_t{1} << index(j);
    };
    const auto faulted = evr::arm::plantForearmFault(a);
    CHECK(faulted.zeroScale == bit(ArmJoint::UpperArm));
    for (const ArmJoint j : {ArmJoint::Attach, ArmJoint::Hand, ArmJoint::Roll3, ArmJoint::Roll2,
                             ArmJoint::Roll1, ArmJoint::ForeArmRoll, ArmJoint::ForeArm}) {
        CHECK(approxPose(at(faulted.joints, j), at(a, j)));
    }
    CHECK(approxPose(at(faulted.joints, ArmJoint::UpperArm), at(a, ArmJoint::ForeArm)));
    // Unlike hands only, whose fold puts every forearm joint on the wrist, scaled to nothing.
    const auto fold = evr::arm::handsOnly(a, evr::arm::Cuff::Fold);
    CHECK((fold.zeroScale & bit(ArmJoint::Roll3)) != 0u);
    CHECK((faulted.zeroScale & bit(ArmJoint::Roll3)) == 0u);
    CHECK(length(at(faulted.joints, ArmJoint::ForeArm).position - at(a, ArmJoint::Hand).position) > 0.2f);
    CHECK(approxPose(at(fold.joints, ArmJoint::ForeArm), at(a, ArmJoint::Hand)));
}
