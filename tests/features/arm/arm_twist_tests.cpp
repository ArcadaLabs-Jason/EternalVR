#include "features/arm/arm_twist.hpp"

#include "features/arm/arm_fixture.hpp"
#include "features/arm/arm_solve.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>

using evr::Vec3;
using evr::arm::ArmJoint;
using evr::arm::ArmPoses;
using evr::arm::ArmTargets;
using evr::arm::index;
using evr::arm::kTwistLimits;
using evr::arm::limitSwing;
using evr::arm::neutralFrame;
using evr::arm::rotateAbout;
using evr::arm::solveArm;
using evr::arm::splitTwist;
using evr::arm::twistAbout;
using evr::arm::unwrapTwist;
using evr::test::approxAxis;
using evr::test::approxEqual;
using evr::test::bindArm;
using evr::test::bindRightArm;
using evr::xr_math::IdViewAxis;
using evr::xr_math::ModelPose;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kRad = kPi / 180.0f;

const ModelPose& at(const ArmPoses& poses, ArmJoint joint) {
    return poses[index(joint)];
}

Vec3 bendNormal(const ArmPoses& a) {
    const Vec3 s = at(a, ArmJoint::UpperArm).position;
    const Vec3 e = at(a, ArmJoint::ForeArm).position;
    const Vec3 w = at(a, ArmJoint::Hand).position;
    return cross(e - s, w - e);
}

Vec3 animatedPole(const ArmPoses& a) {
    const Vec3 s = at(a, ArmJoint::UpperArm).position;
    const Vec3 dir = normalize(at(a, ArmJoint::Hand).position - s);
    const Vec3 e = at(a, ArmJoint::ForeArm).position - s;
    return e - dir * dot(e, dir);
}

} // namespace

TEST_CASE("the neutral frames rebuild the bind pose of both arms") {
    for (const auto& [a, side] : {std::pair{bindArm(), 1.0f}, std::pair{bindRightArm(), -1.0f}}) {
        CAPTURE(side);
        const Vec3 s = at(a, ArmJoint::UpperArm).position;
        const Vec3 e = at(a, ArmJoint::ForeArm).position;
        const Vec3 w = at(a, ArmJoint::Hand).position;
        IdViewAxis forearm;
        IdViewAxis upper;
        REQUIRE(neutralFrame(w - e, bendNormal(a), side, forearm));
        REQUIRE(neutralFrame(e - s, bendNormal(a), side, upper));
        // The wrist, the roll joints and the elbow share the forearm's; the shoulder joint has the upper
        // arm's.
        for (const ArmJoint j : {ArmJoint::Hand, ArmJoint::Roll3, ArmJoint::Roll2, ArmJoint::Roll1,
                                 ArmJoint::ForeArmRoll, ArmJoint::ForeArm}) {
            CHECK(approxAxis(forearm, at(a, j).axis, 2e-4f));
        }
        CHECK(approxAxis(upper, at(a, ArmJoint::UpperArm).axis, 2e-4f));
    }
    IdViewAxis out;
    CHECK_FALSE(neutralFrame({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 1.0f, out));
    CHECK_FALSE(neutralFrame({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 2.0f}, 1.0f, out));
}

TEST_CASE("the twist keeps its way round past half a turn, up to the hysteresis") {
    const float c = kTwistLimits.centre;
    // Fresh: within half a turn of the centre.
    CHECK(approxEqual(unwrapTwist(0.5f, std::nullopt), 0.5f));
    CHECK(approxEqual(unwrapTwist(170.0f * kRad, std::nullopt), 170.0f * kRad - 2.0f * kPi));
    CHECK(unwrapTwist(170.0f * kRad, std::nullopt) > c - kPi);
    // A twist turning steadily keeps going past half a turn from the centre...
    float last = 0.0f;
    float raw = 0.0f;
    for (int i = 0; i < 34; ++i) {
        raw = std::remainder(raw + 5.0f * kRad, 2.0f * kPi);
        const float t = unwrapTwist(raw, last);
        CHECK(approxEqual(t - last, 5.0f * kRad, 1e-4f));
        last = t;
    }
    CHECK(last > c + kPi);
    // ...until it is the hysteresis past it, where it flips by a whole turn.
    CHECK(approxEqual(unwrapTwist(c + kPi + kTwistLimits.hysteresis + 0.01f - 2.0f * kPi, c + kPi + 0.2f),
                      c + kPi + kTwistLimits.hysteresis + 0.01f - 2.0f * kPi, 1e-4f));
    CHECK(approxEqual(unwrapTwist(c - kPi - kTwistLimits.hysteresis - 0.01f + 2.0f * kPi, c - kPi - 0.2f),
                      c - kPi - kTwistLimits.hysteresis - 0.01f + 2.0f * kPi, 1e-4f));
    CHECK(std::isfinite(unwrapTwist(NAN, 0.0f)));
}

TEST_CASE("the forearm takes the twist within its range, the upper arm the rest, the wrist what is left") {
    const float c = kTwistLimits.centre;
    const float f = kTwistLimits.forearm;
    const float u = kTwistLimits.upper;
    auto s = splitTwist(c + 0.5f);
    CHECK(approxEqual(s.forearm, c + 0.5f));
    CHECK(approxEqual(s.upper, 0.0f));
    CHECK(approxEqual(s.wrist, 0.0f));
    s = splitTwist(c + f + 0.4f);
    CHECK(approxEqual(s.forearm, c + f));
    CHECK(approxEqual(s.upper, 0.4f));
    CHECK(approxEqual(s.wrist, 0.0f));
    s = splitTwist(c - f - u - 0.3f);
    CHECK(approxEqual(s.forearm, c - f));
    CHECK(approxEqual(s.upper, -u));
    CHECK(approxEqual(s.wrist, -0.3f));
}

TEST_CASE("the swing is limited to the ellipse and the twist kept") {
    const IdViewAxis n = at(bindArm(), ArmJoint::Hand).axis;
    const IdViewAxis twisted = rotateAbout(n, n.left, 0.7f);
    // Within: unchanged.
    const IdViewAxis small = rotateAbout(twisted, n.forward, 30.0f * kRad);
    auto r = limitSwing(n, small);
    CHECK_FALSE(r.clamped);
    CHECK(approxAxis(r.hand, small));
    CHECK(approxEqual(r.flex, 30.0f * kRad, 1e-3f));
    // Flexed and deviated beyond: brought back onto the ellipse along the same direction.
    const Vec3 tilt = normalize(n.forward + n.up);
    const IdViewAxis far = rotateAbout(twisted, tilt, 80.0f * kRad);
    r = limitSwing(n, far);
    CHECK(r.clamped);
    const float fx = r.flex / kTwistLimits.flex;
    const float dz = r.deviation / kTwistLimits.deviation;
    CHECK(std::sqrt(fx * fx + dz * dz) > 1.0f);
    const auto again = limitSwing(n, r.hand);
    const float gx = again.flex / kTwistLimits.flex;
    const float gz = again.deviation / kTwistLimits.deviation;
    CHECK(approxEqual(std::sqrt(gx * gx + gz * gz), 1.0f, 1e-3f));
    CHECK(approxEqual(again.flex / again.deviation, r.flex / r.deviation, 1e-3f));
    // The twist about the forearm is the same before and after.
    CHECK(approxEqual(twistAbout(n, r.hand, n.left), twistAbout(n, far, n.left), 2e-3f));
}

TEST_CASE("twist past the forearm's range turns the elbow, and the shoulder as far as it lines up") {
    const ArmPoses a = bindArm();
    const ModelPose& hand = at(a, ArmJoint::Hand);
    const Vec3 along = normalize(hand.position - at(a, ArmJoint::ForeArm).position);
    const float extra = 25.0f * kRad;
    const float turn = kTwistLimits.centre + kTwistLimits.forearm + extra;
    ArmTargets targets{{hand.position, rotateAbout(hand.axis, along, turn)},
                       at(a, ArmJoint::UpperArm).position,
                       animatedPole(a)};
    targets.twistFrom = evr::arm::TwistFrom::Bind;
    const auto s = solveArm(a, targets);
    REQUIRE(s);
    CHECK(approxEqual(s->twist, turn, 1e-3f));
    CHECK(approxEqual(s->split.upper, extra, 1e-3f));
    CHECK(approxEqual(twistAbout(at(a, ArmJoint::ForeArm).axis, at(s->joints, ArmJoint::ForeArm).axis, along),
                      extra, 1e-3f));
    // The shoulder joint turns about the upper arm by the part along the forearm (cos of the elbow's bend).
    const Vec3 upperAxis = normalize(at(a, ArmJoint::ForeArm).position - at(a, ArmJoint::UpperArm).position);
    CHECK(approxEqual(
        twistAbout(at(a, ArmJoint::UpperArm).axis, at(s->joints, ArmJoint::UpperArm).axis, upperAxis),
        extra * dot(upperAxis, along), 1e-3f));
    // The wrist joint's own twist over the last roll joint stays the share of the forearm's.
    const float wrist =
        twistAbout(at(s->joints, ArmJoint::Roll3).axis, at(s->joints, ArmJoint::Hand).axis, along);
    CHECK(std::fabs(wrist) < 0.25f * (kTwistLimits.centre + kTwistLimits.forearm));
}

TEST_CASE("mirrored hands give the same twist on both arms") {
    const ArmPoses left = bindArm();
    const ArmPoses right = bindRightArm();
    const ModelPose& lh = at(left, ArmJoint::Hand);
    const Vec3 along = normalize(lh.position - at(left, ArmJoint::ForeArm).position);
    ArmTargets l{{lh.position, rotateAbout(lh.axis, along, 0.8f)},
                 at(left, ArmJoint::UpperArm).position,
                 animatedPole(left)};
    ArmTargets r{evr::test::mirrored(l.hand), at(right, ArmJoint::UpperArm).position, animatedPole(right)};
    l.twistFrom = evr::arm::TwistFrom::Bind;
    r.twistFrom = evr::arm::TwistFrom::Bind;
    const auto sl = solveArm(left, l);
    const auto sr = solveArm(right, r);
    REQUIRE(sl);
    REQUIRE(sr);
    CHECK(approxEqual(sl->twist, 0.8f, 1e-3f));
    CHECK(approxEqual(sr->twist, sl->twist, 1e-3f));
}

TEST_CASE(
    "measured from the animated hand the solve is the one before: the game's idle arm solves to itself") {
    // The bind arm with the game's idle twist in it: the wrist turned by -132 degrees, each roll joint by
    // its share.
    ArmPoses a = bindArm();
    const Vec3 elbow = at(a, ArmJoint::ForeArm).position;
    const Vec3 wrist = at(a, ArmJoint::Hand).position;
    const Vec3 along = normalize(wrist - elbow);
    for (const ArmJoint j :
         {ArmJoint::Hand, ArmJoint::Roll3, ArmJoint::Roll2, ArmJoint::Roll1, ArmJoint::ForeArmRoll}) {
        a[index(j)].axis =
            rotateAbout(a[index(j)].axis, along,
                        -132.0f * kRad * evr::arm::twistShare(a[index(j)].position, elbow, wrist));
    }
    ArmTargets targets{at(a, ArmJoint::Hand), at(a, ArmJoint::UpperArm).position, animatedPole(a)};
    targets.twistFrom = evr::arm::TwistFrom::Animated;
    const auto old = solveArm(a, targets);
    REQUIRE(old);
    CHECK(approxEqual(old->twist, 0.0f, 1e-4f));
    for (std::size_t i = 0; i < evr::arm::kArmJointCount; ++i) {
        CHECK(approxAxis(old->joints[i].axis, a[i].axis, 2e-4f));
    }
    // From the bind pose the same hand is -132 degrees of twist: the forearm takes it to -120, the rest
    // turns the elbow.
    targets.twistFrom = evr::arm::TwistFrom::Bind;
    const auto bind = solveArm(a, targets);
    REQUIRE(bind);
    CHECK(approxEqual(bind->twist, -132.0f * kRad, 1e-3f));
    CHECK(approxEqual(bind->split.forearm, kTwistLimits.centre - kTwistLimits.forearm, 1e-4f));
    CHECK(approxEqual(bind->split.upper, -12.0f * kRad, 1e-3f));
}
