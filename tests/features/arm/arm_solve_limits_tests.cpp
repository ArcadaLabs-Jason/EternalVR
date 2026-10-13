// The arm solve's wrist: its twist measured from the bind pose and continuous across half a turn, its swing
// limited (arm_solve.hpp, arm_twist.hpp). These tests use only solveArm's targets and joints, so they also
// build against the solve before the limits, which fails them: it measured the twist from the game's
// animated hand, wrapped it at half a turn every tick and took the controller's full swing.

#include "features/arm/arm_solve.hpp"

#include "features/arm/arm_fixture.hpp"
#include "features/arm/arm_frames.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <tuple>
#include <type_traits>

using evr::Vec3;
using evr::arm::ArmJoint;
using evr::arm::ArmPoses;
using evr::arm::ArmTargets;
using evr::arm::index;
using evr::arm::rotateAbout;
using evr::arm::solveArm;
using evr::arm::twistAbout;
using evr::arm::twistShare;
using evr::test::bindArm;
using evr::xr_math::IdViewAxis;
using evr::xr_math::ModelPose;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDegrees = 180.0f / kPi;

const ModelPose& at(const ArmPoses& poses, ArmJoint joint) {
    return poses[index(joint)];
}

Vec3 animatedPole(const ArmPoses& a) {
    const Vec3 s = at(a, ArmJoint::UpperArm).position;
    const Vec3 dir = normalize(at(a, ArmJoint::Hand).position - s);
    const Vec3 e = at(a, ArmJoint::ForeArm).position - s;
    return e - dir * dot(e, dir);
}

// The last tick's twist, where the solve takes one (the solve before the limits had none).
template <typename Targets>
void setLastTwist(Targets& targets, std::optional<float> last) {
    if constexpr (requires { targets.lastTwist; }) {
        targets.lastTwist = last;
    }
}

// The bind pose's solve, where the solve has a choice (the layer's default is the animated hand's; the solve
// before the limits had only that one).
template <typename Targets>
void useBindSolve(Targets& targets) {
    if constexpr (requires { targets.twistFrom; }) {
        targets.twistFrom = std::remove_cvref_t<decltype(targets.twistFrom)>::Bind;
    }
}

constexpr ArmJoint kChain[] = {ArmJoint::Hand,  ArmJoint::Roll3,       ArmJoint::Roll2,
                               ArmJoint::Roll1, ArmJoint::ForeArmRoll, ArmJoint::ForeArm};

// The largest twist (degrees) between neighbouring joints of the wrist-to-elbow chain, about the forearm.
// In the bind pose they all share one axis, so this is the twist the skin between them has to take.
float largestTwistStep(const ArmPoses& joints) {
    const Vec3 along =
        normalize(at(joints, ArmJoint::Hand).position - at(joints, ArmJoint::ForeArm).position);
    float worst = 0.0f;
    for (std::size_t i = 0; i + 1 < std::size(kChain); ++i) {
        worst = std::max(
            worst, std::fabs(twistAbout(at(joints, kChain[i + 1]).axis, at(joints, kChain[i]).axis, along)) *
                       kDegrees);
    }
    return worst;
}

// The angle (degrees) of the rotation from `a` to `b`.
float turnBetween(const IdViewAxis& a, const IdViewAxis& b) {
    const float c = (dot(a.forward, b.forward) + dot(a.left, b.left) + dot(a.up, b.up) - 1.0f) * 0.5f;
    return std::acos(std::clamp(c, -1.0f, 1.0f)) * kDegrees;
}

// The left wrist's swing (degrees): the angle between the forearm's line (elbow to wrist) and the hand's y,
// which lie along each other in the left arm's bind pose.
float swingOf(const ArmPoses& joints) {
    const Vec3 along =
        normalize(at(joints, ArmJoint::Hand).position - at(joints, ArmJoint::ForeArm).position);
    const float c = dot(along, at(joints, ArmJoint::Hand).axis.left);
    return std::acos(std::clamp(c, -1.0f, 1.0f)) * kDegrees;
}

// The bind arm with the game's idle twist in it: the wrist turned by `turn` about the forearm and each
// roll joint by its share, as the left arm's shotgun idle has it (about -132 degrees on the rig).
ArmPoses animatedWithTwist(float turn) {
    ArmPoses a = bindArm();
    const Vec3 elbow = at(a, ArmJoint::ForeArm).position;
    const Vec3 wrist = at(a, ArmJoint::Hand).position;
    const Vec3 along = normalize(wrist - elbow);
    for (const ArmJoint j :
         {ArmJoint::Hand, ArmJoint::Roll3, ArmJoint::Roll2, ArmJoint::Roll1, ArmJoint::ForeArmRoll}) {
        a[index(j)].axis =
            rotateAbout(a[index(j)].axis, along, turn * twistShare(a[index(j)].position, elbow, wrist));
    }
    return a;
}

} // namespace

TEST_CASE("a palm turned up twists the forearm evenly, whatever twist the game's animated hand has") {
    // The two-handed hold's palm up is about 55 to 66 degrees from the bind pose on the rig; the game's idle
    // hand about -132.
    const ArmPoses a = animatedWithTwist(-132.0f / kDegrees);
    const ArmPoses bind = bindArm();
    const Vec3 along = normalize(at(bind, ArmJoint::Hand).position - at(bind, ArmJoint::ForeArm).position);
    ArmTargets targets{at(bind, ArmJoint::Hand), at(a, ArmJoint::UpperArm).position, animatedPole(a)};
    useBindSolve(targets);
    targets.hand.axis = rotateAbout(targets.hand.axis, along, 55.0f / kDegrees);
    const auto s = solveArm(a, targets);
    REQUIRE(s);
    // Shared out along the forearm: no two neighbours more than a quarter of it apart. Measured from the
    // animated hand instead, the 187 degrees from it wrap to -173 and the last roll joint ends up about 67
    // degrees from the wrist.
    CAPTURE(largestTwistStep(s->joints));
    CHECK(largestTwistStep(s->joints) < 15.0f);
    // The elbow joint keeps the bind pose's relation to the forearm: no twist of its own.
    CHECK(std::fabs(twistAbout(at(bind, ArmJoint::ForeArm).axis, at(s->joints, ArmJoint::ForeArm).axis,
                               along)) < 1e-3f);
}

TEST_CASE("a controller rolled through more than a full turn turns no forearm joint more than it turns") {
    // The controller rolls about an axis 15 degrees off the forearm (a flex, inside the swing limit), 5
    // degrees a tick, from -205 to 175 degrees: across half a turn from the bind pose.
    const ArmPoses a = animatedWithTwist(-132.0f / kDegrees);
    const ArmPoses bind = bindArm();
    const ModelPose& hand = at(bind, ArmJoint::Hand);
    const Vec3 along = normalize(hand.position - at(bind, ArmJoint::ForeArm).position);
    const Vec3 rollAxis = normalize(rotateAbout(along, hand.axis.forward, 15.0f / kDegrees));
    const auto sweep = [&](bool keepTwist) {
        std::optional<float> last;
        std::optional<ArmPoses> previous;
        float worstHand = 0.0f;
        float worstJoint = 0.0f;
        for (int r = -205; r <= 175; r += 5) {
            ArmTargets targets{
                {hand.position, rotateAbout(hand.axis, rollAxis, static_cast<float>(r) / kDegrees)},
                at(a, ArmJoint::UpperArm).position,
                animatedPole(a)};
            useBindSolve(targets);
            setLastTwist(targets, keepTwist ? last : std::nullopt);
            const auto s = solveArm(a, targets);
            REQUIRE(s);
            last = s->twist;
            if (previous) {
                worstHand = std::max(worstHand, turnBetween(at(*previous, ArmJoint::Hand).axis,
                                                            at(s->joints, ArmJoint::Hand).axis));
                for (const ArmJoint j : {ArmJoint::Roll3, ArmJoint::Roll2, ArmJoint::Roll1,
                                         ArmJoint::ForeArmRoll, ArmJoint::ForeArm, ArmJoint::UpperArm}) {
                    worstJoint =
                        std::max(worstJoint, turnBetween(at(*previous, j).axis, at(s->joints, j).axis));
                }
            }
            previous = s->joints;
        }
        CHECK(worstHand < 5.01f);
        return worstJoint;
    };
    // Kept from tick to tick: no forearm or upper arm joint turns more than about the controller's 5
    // degrees (the twist of a hand turned about an axis off the forearm runs a little faster or slower
    // than the turn itself).
    const float kept = sweep(true);
    CAPTURE(kept);
    CHECK(kept <= 5.5f);
    // Positive control: measured afresh every tick, the twist flips as it passes half a turn from the
    // centre, and a roll joint turns most of a turn in one tick.
    const float fresh = sweep(false);
    CAPTURE(fresh);
    CHECK(fresh > 90.0f);
}

TEST_CASE("the wrist bends no further than a wrist does") {
    const ArmPoses a = bindArm();
    const ModelPose& hand = at(a, ArmJoint::Hand);
    // Flexed 100 degrees toward the palm (about the hand's x), then deviated 60 toward the thumb (about z).
    for (const auto& [axis, degrees, limit] :
         {std::tuple{hand.axis.forward, 100.0f, 70.0f}, std::tuple{hand.axis.forward, -100.0f, 70.0f},
          std::tuple{hand.axis.up, 60.0f, 35.0f}, std::tuple{hand.axis.up, -60.0f, 35.0f}}) {
        CAPTURE(degrees);
        ArmTargets targets{{hand.position, rotateAbout(hand.axis, axis, degrees / kDegrees)},
                           at(a, ArmJoint::UpperArm).position,
                           animatedPole(a)};
        useBindSolve(targets);
        const auto s = solveArm(a, targets);
        REQUIRE(s);
        CAPTURE(swingOf(s->joints));
        CHECK(swingOf(s->joints) <= limit + 0.05f);
        CHECK(swingOf(s->joints) >= limit - 0.05f);
        // The wrist stays on its target; only its bend is limited.
        CHECK(length(at(s->joints, ArmJoint::Hand).position - hand.position) < 1e-4f);
    }
    // Within the limits the hand takes the target's orientation as it is.
    ArmTargets within{{hand.position, rotateAbout(hand.axis, hand.axis.forward, 40.0f / kDegrees)},
                      at(a, ArmJoint::UpperArm).position,
                      animatedPole(a)};
    useBindSolve(within);
    const auto s = solveArm(a, within);
    REQUIRE(s);
    CHECK(turnBetween(at(s->joints, ArmJoint::Hand).axis, within.hand.axis) < 0.05f);
}

TEST_CASE("the weapon arm's wrist stays the game's, bent or not") {
    ArmPoses a = bindArm();
    ModelPose& hand = a[index(ArmJoint::Hand)];
    hand.axis = rotateAbout(hand.axis, hand.axis.forward, 100.0f / kDegrees);
    const Vec3 shoulder = hand.position + Vec3{-0.3f, 0.15f, 0.2f};
    const auto s = evr::arm::solveArmToWrist(a, shoulder, {-0.2f, 0.6f, -1.0f});
    REQUIRE(s);
    CHECK(turnBetween(at(s->joints, ArmJoint::Hand).axis, hand.axis) < 1e-3f);
}
