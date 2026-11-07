// The weapon hand's index finger on the trigger (features/arm/trigger_finger.hpp).

#include "features/arm/trigger_finger.hpp"

#include "features/arm/arm_frames.hpp"
#include "features/arm/grip_hand.hpp"
#include "features/arm/hand_pose.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>

using evr::Quat;
using evr::Vec3;
using evr::arm::ArmSide;
using evr::arm::followsTrigger;
using evr::arm::HandLocal;
using evr::arm::holdsGun;
using evr::arm::IndexChoice;
using evr::arm::indexChoice;
using evr::arm::kHandJointCount;
using evr::arm::kHandJointParents;
using evr::arm::kIndexJoints;
using evr::arm::kRestOpen;
using evr::arm::openIndex;
using evr::arm::TriggerFinger;
using evr::arm::triggerOpen;
using evr::arm::TriggerSensors;
using evr::xr_math::ModelPose;

namespace {

constexpr float kDegrees = 180.0f / std::numbers::pi_v<float>;
constexpr float kLift = 0.7f;

// The turn (degrees) of a joint in its parent's frame, and its axis.
float turnDegrees(const ModelPose& local) {
    const Quat q = evr::arm::toQuat(local.axis);
    return 2.0f * std::atan2(length(Vec3{q.x, q.y, q.z}), std::fabs(q.w)) * kDegrees;
}
Vec3 turnAxis(const ModelPose& local) {
    Quat q = evr::arm::toQuat(local.axis);
    if (q.w < 0.0f) {
        q = {-q.x, -q.y, -q.z, -q.w};
    }
    return normalize(Vec3{q.x, q.y, q.z});
}

// The angle (degrees) between two orientations.
float apartDegrees(const ModelPose& a, const ModelPose& b) {
    const Quat q = conjugate(evr::arm::toQuat(a.axis)) * evr::arm::toQuat(b.axis);
    return 2.0f * std::atan2(length(Vec3{q.x, q.y, q.z}), std::fabs(q.w)) * kDegrees;
}

// The index finger's tip under a wrist at the origin: its last bone carried 22 mm further.
Vec3 indexTip(const HandLocal& hand) {
    std::array<ModelPose, kHandJointCount> model;
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        const ModelPose parent =
            kHandJointParents[i] < 0 ? ModelPose{} : model[static_cast<std::size_t>(kHandJointParents[i])];
        model[i] = evr::arm::compose(parent, hand[i]);
    }
    // The last bone runs on along its own offset from its parent (-y on the right hand, +y on the left).
    const Vec3 along = normalize(hand[kIndexJoints[2]].position) * 0.022f;
    return evr::arm::compose(model[kIndexJoints[2]], ModelPose{along, {}}).position;
}

} // namespace

TEST_CASE(
    "off the trigger the finger lifts by the gun's share; touched it rests; pulled it closes to the grip") {
    // Lifted: the trigger's touch sensed, not touched, not pulled.
    CHECK(triggerOpen({0.0f, false}, true, kLift) == kLift);
    CHECK(triggerOpen({0.01f, false}, true, kLift) == kLift); // an analog trigger at rest
    // Resting, then pulled: kRestOpen falling to the game's grip (0).
    CHECK(triggerOpen({0.0f, true}, true, kLift) == kRestOpen);
    CHECK(triggerOpen({0.5f, true}, true, kLift) == doctest::Approx(kRestOpen * 0.5f));
    CHECK(triggerOpen({1.0f, true}, true, kLift) == 0.0f);
    // A pull with no touch reported is still a finger on the trigger.
    CHECK(triggerOpen({0.6f, false}, true, kLift) == doctest::Approx(kRestOpen * 0.4f));
    // No touch sensed since the controller was tracked (a controller without the sensor): resting, never
    // lifted.
    CHECK(triggerOpen({0.0f, false}, false, kLift) == kRestOpen);
    CHECK(triggerOpen({1.0f, false}, false, kLift) == 0.0f);
    // Out of range or not a number: clamped, never past the grip or straight.
    CHECK(triggerOpen({1.7f, true}, true, kLift) == 0.0f);
    CHECK(triggerOpen({std::nanf(""), true}, true, kLift) == kRestOpen);
    CHECK(triggerOpen({0.0f, false}, true, 1.5f) == 1.0f);
    CHECK(triggerOpen({0.0f, false}, true, -0.5f) == 0.0f);
    CHECK(triggerOpen({0.0f, false}, true, std::nanf("")) == 0.0f);
}

TEST_CASE(
    "the touch is sensed once seen, forgotten while the controller is not tracked, and the share eases") {
    TriggerFinger f;
    // A controller that has not reported a touch yet: the finger rests on the trigger.
    CHECK(f.update({0.0f, false}, true, kLift, 0.0f) == kRestOpen);
    CHECK_FALSE(f.touchSensed());
    CHECK(f.update({0.0f, true}, true, kLift, 1.0f) == doctest::Approx(kRestOpen));
    CHECK(f.touchSensed());
    // Let go: eases toward the lift, two thirds of the way in kSeconds.
    const float step = f.update({0.0f, false}, true, kLift, TriggerFinger::kSeconds);
    CHECK(step == doctest::Approx(kRestOpen + (kLift - kRestOpen) * (1.0f - std::exp(-1.0f))).epsilon(1e-4));
    CHECK(f.update({0.0f, false}, true, kLift, 1.0f) == doctest::Approx(kLift).epsilon(1e-4));
    // Lost: the sensor forgotten (another controller may come back), the finger back on the trigger.
    f.update({0.0f, false}, false, kLift, 1.0f);
    CHECK_FALSE(f.touchSensed());
    CHECK(f.update({0.0f, false}, true, kLift, 1.0f) == doctest::Approx(kRestOpen).epsilon(1e-4));
    // After a reset the first update takes the share as it is.
    f.update({0.0f, true}, true, kLift, 1.0f);
    f.reset();
    CHECK(f.update({0.0f, false}, true, kLift, 0.001f) == kLift);
    // A bad time step does not move it.
    CHECK(f.update({1.0f, true}, true, kLift, std::nanf("")) == kLift);
}

TEST_CASE("the index finger opens along its own bend: no share is the game's grip, all of it straight") {
    const HandLocal& grip = evr::arm::heavyCannonGrip().hand;
    const HandLocal same = openIndex(grip, 0.0f);
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        CHECK(apartDegrees(same[i], grip[i]) < 0.01f);
    }
    const HandLocal straight = openIndex(grip, 1.0f);
    const HandLocal half = openIndex(grip, 0.5f);
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        INFO(i);
        CHECK(straight[i].position == grip[i].position);
        const bool index = i == kIndexJoints[0] || i == kIndexJoints[1] || i == kIndexJoints[2];
        if (!index) {
            CHECK(apartDegrees(straight[i], grip[i]) < 1e-3f);
            continue;
        }
        CHECK(turnDegrees(straight[i]) < 0.01f);
        // Half the turn about the same axis: on the arc from the grip to straight, never off it.
        CHECK(turnDegrees(half[i]) == doctest::Approx(turnDegrees(grip[i]) * 0.5f).epsilon(1e-3));
        CHECK(dot(turnAxis(half[i]), turnAxis(grip[i])) > 0.9999f);
        CHECK(apartDegrees(half[i], grip[i]) + apartDegrees(half[i], straight[i]) ==
              doctest::Approx(turnDegrees(grip[i])).epsilon(1e-3));
    }
    // The grip's own bend is there to open: the Heavy Cannon's index finger, 24, 46 and 80 degrees.
    CHECK(turnDegrees(grip[kIndexJoints[0]]) == doctest::Approx(24.4f).epsilon(0.02));
    CHECK(turnDegrees(grip[kIndexJoints[1]]) == doctest::Approx(45.5f).epsilon(0.02));
    CHECK(turnDegrees(grip[kIndexJoints[2]]) == doctest::Approx(79.7f).epsilon(0.02));
}

TEST_CASE(
    "the finger's tip moves out from the grip as it opens, a few mm resting, and past it only below 0") {
    for (const HandLocal* grip : {&evr::arm::heavyCannonGrip().hand, &evr::arm::leftGripHand()}) {
        const Vec3 held = indexTip(*grip);
        const float rest = length(indexTip(openIndex(*grip, kRestOpen)) - held);
        CHECK(rest > 0.005f); // the pad off the trigger's face
        CHECK(rest < 0.010f);
        // Further out the more it opens, and further from the palm (the wrist) than the grip holds it.
        float last = 0.0f;
        for (const float open : {0.2f, 0.4f, kLift, 1.0f}) {
            INFO(open);
            const Vec3 tip = indexTip(openIndex(*grip, open));
            CHECK(length(tip - held) > last);
            last = length(tip - held);
            CHECK(length(tip) > length(held));
        }
        // The rig's planted fault: past the grip, the tip toward the palm.
        CHECK(length(indexTip(openIndex(*grip, -0.6f))) < length(held) - 0.01f);
    }
}

TEST_CASE("the finger follows the trigger on the New arms path only, as the off hand's fingers") {
    CHECK(followsTrigger(true, true));        // the free off hand and the bind solve: the launcher's default
    CHECK_FALSE(followsTrigger(true, false)); // the animated solve
    CHECK_FALSE(followsTrigger(false, true)); // the game's off hand
    CHECK_FALSE(followsTrigger(false, false));
}

TEST_CASE("the arm that holds the gun: the weapon arm, or the left arm with the weapon in the left hand") {
    CHECK(holdsGun(true, ArmSide::Right));        // the weapon arm's own tick
    CHECK(holdsGun(false, ArmSide::Left));        // the weapon in the left hand: the left glove on the gun
    CHECK_FALSE(holdsGun(false, ArmSide::Right)); // ... and the right glove is the off hand's
}

TEST_CASE(
    "the index finger's choice: the glove's grip, the game's grip read for this item, else the game's") {
    // No share: the game's finger, nothing written.
    IndexChoice c = indexChoice(std::nullopt, false, true, 1.0f);
    CHECK(c.from == IndexChoice::From::Game);
    CHECK(c.mask == 0);
    // The glove on a grip: its grip opened by the share as it is (the glove's own curl carries the weight).
    c = indexChoice(0.7f, true, false, 0.5f);
    CHECK(c.from == IndexChoice::From::Glove);
    CHECK(c.open == 0.7f);
    CHECK(c.mask == 0);
    // The game's grip read clean for the item held now: the share by the arm's weight, the three bones only.
    c = indexChoice(0.7f, false, true, 0.5f);
    CHECK(c.from == IndexChoice::From::Grip);
    CHECK(c.open == doctest::Approx(0.35f));
    CHECK(c.mask == evr::arm::kIndexModMask);
    CHECK(c.mask == 0x38);
    // A grip read for another item (a switch with no clean read since): the game's finger until one is read.
    c = indexChoice(0.7f, false, false, 1.0f);
    CHECK(c.from == IndexChoice::From::Game);
    CHECK(c.mask == 0);
    // A weight out of range is clamped; the planted fault's negative share passes through.
    CHECK(indexChoice(0.7f, false, true, 3.0f).open == doctest::Approx(0.7f));
    CHECK(indexChoice(-0.6f, false, true, 1.0f).open == doctest::Approx(-0.6f));
    CHECK(indexChoice(0.7f, false, true, std::nanf("")).open == 0.0f);
}
