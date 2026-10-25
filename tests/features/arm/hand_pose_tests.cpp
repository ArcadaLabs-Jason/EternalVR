// The free off hand's fingers from its controller (features/arm/hand_pose.hpp).

#include "features/arm/hand_pose.hpp"

#include "features/arm/arm_fixture.hpp"
#include "features/arm/arm_frames.hpp"
#include "features/arm/left_hand.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

using evr::Vec3;
using evr::arm::fingerCurls;
using evr::arm::FingerCurls;
using evr::arm::FingerEase;
using evr::arm::FingerSensors;
using evr::arm::HandLocal;
using evr::arm::kHandJointCount;
using evr::arm::kHandJointParents;
using evr::test::approxAxis;
using evr::test::approxEqual;
using evr::xr_math::ModelPose;

namespace {

constexpr float kDegrees = 180.0f / std::numbers::pi_v<float>;

// The turn (degrees) of a joint in its parent's frame.
float turnDegrees(const ModelPose& local) {
    const auto q = evr::arm::toQuat(local.axis);
    return 2.0f * std::atan2(length(Vec3{q.x, q.y, q.z}), std::fabs(q.w)) * kDegrees;
}

// The model-space positions of the hand's joints under a wrist at the origin, the finger tips' ends included
// (each finger's last bone carried one more bone length along its -y).
std::array<Vec3, kHandJointCount> modelPositions(const HandLocal& hand) {
    std::array<ModelPose, kHandJointCount> model;
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        const ModelPose parent =
            kHandJointParents[i] < 0 ? ModelPose{} : model[static_cast<std::size_t>(kHandJointParents[i])];
        model[i] = evr::arm::compose(parent, hand[i]);
    }
    std::array<Vec3, kHandJointCount> out;
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        out[i] = model[i].position;
    }
    return out;
}

} // namespace

TEST_CASE(
    "the fingers follow the controller: grip closes three, the trigger the index, a thumb touch the thumb") {
    const FingerCurls open = fingerCurls({});
    CHECK(open.thumb == 0.0f);
    CHECK(open.index == 0.0f);
    CHECK(open.middle == 0.0f);
    CHECK(open.ring == 0.0f);
    CHECK(open.pinky == 0.0f);
    const FingerCurls fist = fingerCurls({1.0f, 1.0f, true, true});
    CHECK(fist.thumb == 1.0f);
    CHECK(fist.index == 1.0f);
    CHECK(fist.middle == 1.0f);
    CHECK(fist.pinky == 1.0f);
    const FingerCurls half = fingerCurls({0.5f, 0.0f, true, false});
    CHECK(half.middle == 0.5f);
    CHECK(half.ring == 0.5f);
    CHECK(half.index == evr::arm::kRestingIndexCurl); // resting on the trigger
    CHECK(half.thumb == 0.0f);
    CHECK(fingerCurls({0.0f, 0.8f, true, false}).index == 0.8f);
    // Out of range or not a number: clamped, never past closed or open.
    const FingerCurls odd = fingerCurls({1.7f, std::nanf(""), false, false});
    CHECK(odd.middle == 1.0f);
    CHECK(odd.index == 0.0f);
}

TEST_CASE("the curls are eased: a touch that flips moves the finger over a few frames, not at once") {
    FingerEase ease;
    CHECK(ease.update({1.0f, 0.0f, 0.0f, 0.0f, 0.0f}, 0.011f).thumb == 1.0f); // the first update as it is
    const float afterOne = ease.update({}, 0.011f).thumb;
    CHECK(afterOne < 1.0f);
    CHECK(afterOne > 0.7f);
    float t = afterOne;
    for (int i = 0; i < 90; ++i) {
        t = ease.update({}, 0.011f).thumb;
    }
    CHECK(t < 0.01f);
    CHECK(ease.update({}, std::nanf("")).thumb == t); // a bad step changes nothing
}

TEST_CASE(
    "the open hand straightens every finger bone and keeps the roots, the thumb's base and the lengths") {
    const HandLocal& grip = evr::arm::leftGripHand();
    const HandLocal open = evr::arm::openHand(grip);
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        CAPTURE(i);
        CHECK(approxEqual(open[i].position, grip[i].position));
        if (kHandJointParents[i] < 0) {
            CHECK(approxAxis(open[i].axis, grip[i].axis));
        } else {
            CHECK(turnDegrees(open[i]) < 1e-3f);
        }
    }
    // The grip is closed: the middle finger's three bones turn about 66, 85 and 52 degrees (the right hand's
    // idle).
    CHECK(turnDegrees(grip[8]) > 50.0f);
    CHECK(turnDegrees(grip[9]) > 50.0f);
}

TEST_CASE("the posed hand: open at no curl, the game's grip at full, each finger on its own") {
    const HandLocal& grip = evr::arm::leftGripHand();
    const HandLocal open = evr::arm::openHand(grip);
    const HandLocal& offsets = grip;
    const HandLocal none = evr::arm::posedHand(open, grip, {}, offsets);
    const HandLocal full = evr::arm::posedHand(open, grip, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, offsets);
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        CAPTURE(i);
        CHECK(approxAxis(none[i].axis, open[i].axis));
        CHECK(approxAxis(full[i].axis, grip[i].axis));
    }
    // Only the index curled: the index's bones bend, the middle finger's stay straight.
    const HandLocal pointing = evr::arm::posedHand(open, grip, {0.0f, 1.0f, 0.0f, 0.0f, 0.0f}, offsets);
    CHECK(turnDegrees(pointing[5]) > 30.0f);
    CHECK(turnDegrees(pointing[9]) < 1e-3f);
    // The offsets are the model's own bone lengths.
    HandLocal longer = grip;
    longer[9].position = longer[9].position * 1.2f;
    CHECK(approxEqual(evr::arm::posedHand(open, grip, {}, longer)[9].position, longer[9].position));
}

TEST_CASE("the closed hand's finger tips curl back toward the palm; the open hand's reach out along it") {
    const HandLocal& grip = evr::arm::leftGripHand();
    const HandLocal open = evr::arm::openHand(grip);
    const auto closedAt = modelPositions(grip);
    const auto openAt = modelPositions(open);
    // The middle finger's last joint from the wrist: further out with the hand open than closed round a
    // handle.
    CHECK(length(openAt[10]) > length(closedAt[10]) + 0.03f);
    // Open, its joints lie on one line out of its root (a straight finger).
    const Vec3 a = normalize(openAt[9] - openAt[8]);
    const Vec3 b = normalize(openAt[10] - openAt[9]);
    CHECK(dot(a, b) > 0.999f);
}

TEST_CASE("the fifteen written bones are the finger bones, the roots left out") {
    const HandLocal& grip = evr::arm::leftGripHand();
    const auto writes = evr::arm::fingerWrites(grip);
    for (std::size_t k = 0; k < evr::arm::kFingerModCount; ++k) {
        CHECK(approxEqual(writes[k].position, grip[evr::arm::kFingerMods[k]].position));
    }
    CHECK(evr::arm::fingerOf(0) == 0);
    CHECK(evr::arm::fingerOf(2) == 0);
    CHECK(evr::arm::fingerOf(3) == 1);
    CHECK(evr::arm::fingerOf(6) == 1);
    CHECK(evr::arm::fingerOf(10) == 2);
    CHECK(evr::arm::fingerOf(14) == 3);
    CHECK(evr::arm::fingerOf(18) == 4);
}

TEST_CASE("a controller without trigger touch or thumb touch (a Vive wand): the curls from what it has") {
    // Analog trigger, a grip, no touch sensors at all: the index follows the trigger alone (no resting bend),
    // the thumb stays out, the three fingers follow the grip.
    const FingerCurls idle = fingerCurls({0.0f, 0.0f, false, false});
    CHECK(idle.index == 0.0f);
    CHECK(idle.thumb == 0.0f);
    CHECK(idle.middle == 0.0f);
    const FingerCurls pull = fingerCurls({1.0f, 0.6f, false, false});
    CHECK(pull.index == doctest::Approx(0.6f));
    CHECK(pull.middle == 1.0f);
    CHECK(pull.ring == 1.0f);
    CHECK(pull.pinky == 1.0f);
    CHECK(pull.thumb == 0.0f);
}

TEST_CASE("a controller with only a digital grip: the three fingers close over a few frames, not at once") {
    FingerEase ease;
    CHECK(ease.update(fingerCurls({0.0f, 0.0f, false, false}), 0.011f).middle == 0.0f);
    // The grip clicks to 1: one frame later the fingers are on their way, not closed.
    const FingerCurls one = ease.update(fingerCurls({1.0f, 0.0f, false, false}), 0.011f);
    CHECK(one.middle > 0.1f);
    CHECK(one.middle < 0.25f);
    CHECK(one.ring == doctest::Approx(one.middle));
    CHECK(one.index == 0.0f);
    CHECK(one.thumb == 0.0f);
    FingerCurls c = one;
    for (int i = 0; i < 40; ++i) {
        c = ease.update(fingerCurls({1.0f, 0.0f, false, false}), 0.011f);
    }
    CHECK(c.middle > 0.99f);
    // Let go: open again the same way.
    const FingerCurls back = ease.update(fingerCurls({0.0f, 0.0f, false, false}), 0.011f);
    CHECK(back.middle < 0.9f);
    CHECK(back.middle > 0.75f);
}

TEST_CASE("a controller lost or off: the fingers ease back to the game's hand, and come back the same way") {
    evr::arm::FingerHold hold;
    CHECK(hold.update(true, 0.011f) == 1.0f); // the first update as it is
    const float one = hold.update(false, 0.011f);
    CHECK(one < 1.0f);
    CHECK(one > 0.85f);
    float h = one;
    for (int i = 0; i < 60; ++i) {
        h = hold.update(false, 0.011f);
    }
    CHECK(h < 0.01f); // the game's, about two thirds of a second after the loss
    CHECK(hold.update(false, std::nanf("")) == h);
    const float again = hold.update(true, 0.011f);
    CHECK(again > h);
    CHECK(again < 0.15f);
    hold.reset();
    CHECK(hold.update(false, 0.011f) == 0.0f); // started again: as it is

    // The blend: none of the posed hand at 0, all of it at 1, between in between, the bone offsets kept.
    const HandLocal& grip = evr::arm::leftGripHand();
    const HandLocal open = evr::arm::openHand(grip);
    const HandLocal none = evr::arm::blendHand(grip, open, 0.0f);
    const HandLocal all = evr::arm::blendHand(grip, open, 1.0f);
    const HandLocal half = evr::arm::blendHand(grip, open, 0.5f);
    const HandLocal bad = evr::arm::blendHand(grip, open, std::nanf(""));
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        CAPTURE(i);
        CHECK(approxAxis(none[i].axis, grip[i].axis));
        CHECK(approxAxis(all[i].axis, open[i].axis));
        CHECK(approxAxis(bad[i].axis, grip[i].axis)); // not a number: the game's
        CHECK(approxEqual(half[i].position, grip[i].position));
    }
    // The middle finger's second bone half way between the grip's turn and straight.
    CHECK(turnDegrees(half[9]) == doctest::Approx(turnDegrees(grip[9]) * 0.5f).epsilon(0.01));
}
