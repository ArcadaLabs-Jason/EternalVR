#include "features/arm/mod_mix.hpp"

#include "features/arm/arm_fixture.hpp"
#include "features/arm/arm_frames.hpp"
#include "features/arm/arm_solve.hpp"
#include "xr_math/head_aim.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>

using evr::Vec3;
using evr::arm::ArmJoint;
using evr::arm::ArmPoses;
using evr::arm::compose;
using evr::arm::index;
using evr::arm::kArmJointCount;
using evr::arm::mixedArm;
using evr::arm::mixPose;
using evr::arm::modifiedFor;
using evr::arm::parentFor;
using evr::arm::pastPose;
using evr::arm::preModifierPose;
using evr::arm::relative;
using evr::arm::rotateAbout;
using evr::arm::usableModWeight;
using evr::test::approxPose;
using evr::test::bindArm;
using evr::test::bindRightArm;
using evr::xr_math::applyJointMod;
using evr::xr_math::jointModToward;
using evr::xr_math::ModelPose;

namespace {

constexpr float kWeight = 0.95f; // the hands' modifier node on the rig
constexpr float kRad = 3.14159265f / 180.0f;

// The angle between two orientations, degrees.
float turnDegrees(const evr::xr_math::IdViewAxis& a, const evr::xr_math::IdViewAxis& b) {
    const evr::Quat q = conjugate(evr::arm::toQuat(a)) * evr::arm::toQuat(b);
    return 2.0f * std::atan2(length(Vec3{q.x, q.y, q.z}), std::fabs(q.w)) / kRad;
}

// The arm with the wrist moved and turned, the forearm and upper arm solved to it (bind solve), the attach
// joint carrying the wrist: what the layer asks for.
ArmPoses wantedArm(const ArmPoses& a, Vec3 move, float turn) {
    evr::arm::ArmTargets t;
    t.hand = {a[index(ArmJoint::Hand)].position + move,
              rotateAbout(a[index(ArmJoint::Hand)].axis, normalize(Vec3{0.3f, 0.8f, 0.2f}), turn)};
    t.shoulder = a[index(ArmJoint::UpperArm)].position + Vec3{0.02f, -0.03f, 0.01f};
    t.pole = {-0.2f, 0.6f, -1.0f};
    t.twistFrom = evr::arm::TwistFrom::Bind;
    const auto s = evr::arm::solveArm(a, t);
    REQUIRE(s);
    return s->joints;
}

// The furthest the blend at kWeight takes a turn written short of half a turn (179 degrees): nlerp's angle.
float reachDegrees() {
    const float h = 89.5f * kRad;
    return 2.0f * std::atan2(kWeight * std::sin(h), 1.0f - kWeight + kWeight * std::cos(h)) / kRad;
}

// `a` turned toward `b` along the shortest arc by `t` of the way (slerp, extrapolated above 1): the inverse
// the layer used before round 4.
ModelPose slerpPast(const ModelPose& a, const ModelPose& b, float t) {
    evr::Quat q = evr::arm::rotationBetween(a.axis, b.axis);
    if (q.w < 0.0f) {
        q = {-q.x, -q.y, -q.z, -q.w};
    }
    const Vec3 v{q.x, q.y, q.z};
    const float angle = 2.0f * std::atan2(length(v), q.w);
    return {a.position + (b.position - a.position) * t, rotateAbout(a.axis, normalize(v), angle * t)};
}

float worstMetres(const ArmPoses& a, const ArmPoses& b, std::size_t from) {
    float worst = 0.0f;
    for (std::size_t i = from; i < kArmJointCount; ++i) {
        worst = std::fmax(worst, length(a[i].position - b[i].position));
    }
    return worst;
}

} // namespace

TEST_CASE("a modifier weight is used only above 0.5 and up to 1") {
    CHECK(usableModWeight(0.95f));
    CHECK(usableModWeight(1.0f));
    CHECK_FALSE(usableModWeight(0.5f));
    CHECK_FALSE(usableModWeight(1.01f));
    CHECK_FALSE(usableModWeight(0.0f));
    CHECK_FALSE(usableModWeight(std::numeric_limits<float>::quiet_NaN()));
}

TEST_CASE("the blend turns as nlerp does, and pastPose undoes it") {
    const ModelPose a{{0.1f, 0.2f, 0.3f}, evr::xr_math::axisFromAngles({10.0f, -20.0f, 30.0f})};
    const ModelPose b{{0.4f, -0.1f, 0.2f}, evr::xr_math::axisFromAngles({-50.0f, 40.0f, 100.0f})};
    CHECK(approxPose(mixPose(a, b, 0.0f), a));
    CHECK(approxPose(mixPose(a, b, 1.0f), b));
    CHECK(approxPose(mixPose(a, pastPose(a, b, kWeight), kWeight), b, 1e-4f));
    // The game's blend command (0x19EAE50): a turn of 90 degrees mixed at 0.95 lands 85.88 round, 150 lands
    // 144.26 (slerp's 85.5 and 142.5).
    const Vec3 axis = normalize(Vec3{0.3f, -0.4f, 0.8f});
    for (const auto& [turn, lands] : {std::pair{90.0f, 85.88f}, std::pair{150.0f, 144.26f}}) {
        CAPTURE(turn);
        const ModelPose c{a.position, rotateAbout(a.axis, axis, turn * kRad)};
        CHECK(turnDegrees(a.axis, mixPose(a, c, kWeight).axis) == doctest::Approx(lands).epsilon(1e-3));
        // Positive control: written past by 1 / weight along the arc (the inverse before round 4), the turn
        // lands more than 1.5 degrees past its target at 150 degrees.
        const ModelPose slerped = mixPose(a, slerpPast(a, c, 1.0f / kWeight), kWeight);
        CHECK(approxPose(mixPose(a, pastPose(a, c, kWeight), kWeight), c, 1e-4f));
        if (turn > 100.0f) {
            CHECK(turnDegrees(slerped.axis, c.axis) > 1.5f);
        }
    }
}

TEST_CASE("the off hand's arm written past its target by the weight lands on it") {
    const ArmPoses a = bindArm();
    const ArmPoses wanted = wantedArm(a, {0.12f, -0.08f, 0.1f}, 1.2f);
    // The wanted wrist is where the wanted attach joint carries it.
    REQUIRE(approxPose(wanted[index(ArmJoint::Hand)],
                       compose(wanted[0], relative(a[0], a[index(ArmJoint::Hand)])), 1e-4f));
    const ArmPoses written = modifiedFor(a, wanted, kWeight);
    const ArmPoses landed = mixedArm(a, written, kWeight);
    for (std::size_t i = 0; i < kArmJointCount; ++i) {
        CAPTURE(i);
        CHECK(approxPose(landed[i], wanted[i], 2e-4f));
    }
    // Positive control: written as wanted (the layer before this step), the mix leaves the arm short.
    const ArmPoses asWritten = mixedArm(a, wanted, kWeight);
    CHECK(worstMetres(asWritten, wanted, 0) > 0.01f);
    // At weight 1 nothing changes.
    const ArmPoses whole = modifiedFor(a, wanted, 1.0f);
    for (std::size_t i = 0; i < kArmJointCount; ++i) {
        CHECK(approxPose(whole[i], wanted[i], 1e-4f));
    }
}

TEST_CASE("the weapon arm's joints written past their targets land with the game's wrist") {
    const ArmPoses a = bindRightArm();
    const ModelPose& hand = a[index(ArmJoint::Hand)];
    const auto s = evr::arm::solveArmToWrist(a, hand.position + Vec3{-0.3f, -0.15f, 0.2f},
                                             {-0.2f, -0.6f, -1.0f}, std::nullopt, evr::arm::TwistFrom::Bind);
    REQUIRE(s);
    const ArmPoses& wanted = s->joints;
    const ArmPoses written = modifiedFor(a, wanted, kWeight, &hand);
    CHECK(approxPose(written[0], a[0]));
    CHECK(approxPose(written[index(ArmJoint::Hand)], hand));
    const ArmPoses landed = mixedArm(a, written, kWeight);
    for (std::size_t i = index(ArmJoint::Hand); i < kArmJointCount; ++i) {
        CAPTURE(i);
        CHECK(approxPose(landed[i], wanted[i], 2e-4f));
    }
    CHECK(worstMetres(mixedArm(a, wanted, kWeight), wanted, index(ArmJoint::Roll3)) > 0.005f);
}

TEST_CASE("the pose without modifiers comes back from the read and the change written") {
    const ModelPose x{{0.3f, -0.1f, -0.15f}, evr::xr_math::axisFromAngles({20.0f, 35.0f, -70.0f})};
    const ModelPose target{{0.17f, 0.25f, -0.22f}, evr::xr_math::axisFromAngles({-30.0f, 80.0f, 10.0f})};
    const evr::xr_math::JointMod mod = jointModToward(x, target);
    const ModelPose read = mixPose(x, applyJointMod(x, mod), kWeight);
    CHECK(approxPose(preModifierPose(read, mod, kWeight), x, 1e-4f));
}

TEST_CASE("an attach joint's change from the pre-modifier pose lands; from the read it never does") {
    // The game applies a change to its pose without modifiers (x) and mixes it in at the weight; the layer
    // reads the result of the last frame.
    const ModelPose x{{0.27f, -0.12f, -0.14f}, evr::xr_math::axisFromAngles({5.0f, 60.0f, -40.0f})};
    const ModelPose target{{0.07f, 0.19f, -0.32f}, evr::xr_math::axisFromAngles({-20.0f, 110.0f, 30.0f})};
    const auto frame = [&](const evr::xr_math::JointMod& m) {
        return mixPose(x, applyJointMod(x, m), kWeight);
    };
    // As before this step: the change toward the target from the read.
    ModelPose read = x;
    for (int i = 0; i < 400; ++i) {
        read = frame(jointModToward(read, target));
    }
    CHECK(length(read.position - target.position) > 0.1f);
    // From the pose without modifiers: the read and the change written last give it back.
    read = x;
    evr::xr_math::JointMod last;
    float worst = 0.0f;
    for (int i = 0; i < 5; ++i) {
        const ModelPose pre = preModifierPose(read, last, kWeight);
        last = jointModToward(pre, pastPose(pre, target, kWeight));
        read = frame(last);
        worst = length(read.position - target.position);
    }
    CHECK(worst < 1e-4f);
    CHECK(approxPose(read, target, 1e-4f));
}

TEST_CASE("an attach joint turned further than the mix reaches lands short; the forearm still lands") {
    const ArmPoses a = bindArm();
    const ModelPose& attach = a[0];
    const Vec3 axis = normalize(Vec3{0.2f, 0.9f, -0.3f});
    const float turn = 175.0f * kRad;
    const ModelPose turned{attach.position + Vec3{0.05f, 0.1f, -0.05f}, rotateAbout(attach.axis, axis, turn)};
    const ArmPoses wanted = evr::arm::followAttach(a, turned);
    const ArmPoses landed = mixedArm(a, modifiedFor(a, wanted, kWeight), kWeight);
    // The attach joint and the wrist turn as far as the mix takes them: within 10 degrees of the target.
    CHECK(turnDegrees(landed[0].axis, wanted[0].axis) < 10.0f);
    CHECK(length(landed[0].position - wanted[0].position) < 1e-4f);
    // Every joint after the wrist is placed from where the wrist lands, so it lands on its target.
    for (std::size_t i = index(ArmJoint::Roll3); i < kArmJointCount; ++i) {
        CAPTURE(i);
        CHECK(approxPose(landed[i], wanted[i], 3e-4f));
    }
    // Positive control: written past without the half-turn limit the turn passes half a turn, and the mix
    // takes the attach joint the other way round.
    const ModelPose unclamped = mixPose(attach, slerpPast(attach, wanted[0], 1.0f / kWeight), kWeight);
    CHECK(turnDegrees(unclamped.axis, wanted[0].axis) > 15.0f);
}

TEST_CASE("the furthest an attach joint lands: 179 degrees mixed at 0.95, 172.98") {
    const ArmPoses a = bindArm();
    const Vec3 axis = normalize(Vec3{0.2f, 0.9f, -0.3f});
    CHECK(reachDegrees() == doctest::Approx(172.98).epsilon(1e-4));
    for (const float need : {175.0f, 178.0f}) {
        CAPTURE(need);
        const ArmPoses wanted =
            evr::arm::followAttach(a, {a[0].position, rotateAbout(a[0].axis, axis, need * kRad)});
        const ArmPoses landed = mixedArm(a, modifiedFor(a, wanted, kWeight), kWeight);
        CHECK(turnDegrees(a[0].axis, landed[0].axis) == doctest::Approx(reachDegrees()).epsilon(0.002));
        CHECK(turnDegrees(landed[0].axis, wanted[0].axis) ==
              doctest::Approx(need - reachDegrees()).epsilon(0.03));
    }
    // Positive control: a turn the mix reaches lands on its target.
    const ArmPoses within =
        evr::arm::followAttach(a, {a[0].position, rotateAbout(a[0].axis, axis, 160.0f * kRad)});
    const ArmPoses landed = mixedArm(a, modifiedFor(a, within, kWeight), kWeight);
    CHECK(turnDegrees(landed[0].axis, within[0].axis) < 0.05f);
}

TEST_CASE("the attach joint's last change counts only for the hands it was handed for") {
    using evr::arm::HandedMod;
    using evr::arm::handedTo;
    int handsA = 0;
    int handsB = 0;
    evr::xr_math::JointMod mod;
    mod.translation = {1.0f, 2.0f, 3.0f};
    const std::optional<HandedMod> last = HandedMod{&handsA, mod};
    const auto same = handedTo(last, &handsA);
    REQUIRE(same);
    CHECK(same->translation.y == 2.0f);
    CHECK_FALSE(handedTo(last, &handsB));
    CHECK_FALSE(handedTo(std::nullopt, &handsA));
    CHECK_FALSE(handedTo(last, nullptr));
}

TEST_CASE("the wrist's change takes the rest of a turn the attach joint cannot make; the whole arm lands") {
    const ArmPoses a = bindArm();
    const Vec3 axis = normalize(Vec3{0.2f, 0.9f, -0.3f});
    for (const float need : {175.0f, 177.0f, 179.5f}) {
        CAPTURE(need);
        const ModelPose turned{a[0].position + Vec3{0.05f, 0.1f, -0.05f},
                               rotateAbout(a[0].axis, axis, need * kRad)};
        const ArmPoses wanted = evr::arm::followAttach(a, turned);
        const evr::arm::WristArm w = evr::arm::modifiedWithWrist(a, wanted, kWeight);
        CHECK(w.wristTurned);
        const ArmPoses landed = mixedArm(a, w.modified, kWeight);
        // The attach joint lands as far as the mix takes it; the wrist and everything after it on target.
        CHECK(turnDegrees(a[0].axis, landed[0].axis) == doctest::Approx(reachDegrees()).epsilon(0.002));
        for (std::size_t i = index(ArmJoint::Hand); i < kArmJointCount; ++i) {
            CAPTURE(i);
            CHECK(approxPose(landed[i], wanted[i], 3e-4f));
        }
        // The change on the wrist is what takes it from where the attach joint carries it to where it is
        // written.
        const evr::xr_math::JointMod change = evr::arm::wristChange(a, w);
        const ModelPose carried = compose(w.modified[0], relative(a[0], a[index(ArmJoint::Hand)]));
        CHECK(approxPose(applyJointMod(carried, change), w.modified[index(ArmJoint::Hand)], 1e-4f));
        // Positive control: without the wrist's change the wrist lands short by the rest of the turn.
        const ArmPoses before = mixedArm(a, modifiedFor(a, wanted, kWeight), kWeight);
        CHECK(turnDegrees(before[index(ArmJoint::Hand)].axis, wanted[index(ArmJoint::Hand)].axis) >
              need - reachDegrees() - 0.5f);
    }
}

TEST_CASE("a turn the attach joint reaches leaves the wrist alone, as modifiedFor writes it") {
    const ArmPoses a = bindArm();
    const ArmPoses wanted = wantedArm(a, {0.12f, -0.08f, 0.1f}, 1.2f);
    const evr::arm::WristArm w = evr::arm::modifiedWithWrist(a, wanted, kWeight);
    CHECK_FALSE(w.wristTurned);
    const ArmPoses plain = modifiedFor(a, wanted, kWeight);
    for (std::size_t i = 0; i < kArmJointCount; ++i) {
        CAPTURE(i);
        CHECK(approxPose(w.modified[i], plain[i], 1e-5f));
    }
    const evr::xr_math::JointMod change = evr::arm::wristChange(a, w);
    CHECK(change.translation == Vec3{});
    CHECK(change.rotation == evr::xr_math::JointMod{}.rotation);
}
