#include "features/input/capture_chord.hpp"

#include "features/input/input_frames.hpp"

#include <doctest/doctest.h>

#include <ostream>

using evr::input::CaptureButtons;
using evr::input::CaptureChord;
using evr::input::CaptureChordOutput;
using evr::input::InputFrame;
using evr::test::restingFrame;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

InputFrame frameOf(bool menu, float leftTrigger, float rightTrigger) {
    InputFrame frame = restingFrame();
    frame.left.menuButton = menu;
    frame.left.trigger = leftTrigger;
    frame.right.trigger = rightTrigger;
    return frame;
}

} // namespace

TEST_CASE("a trigger pulled while Menu is held asks for one capture per pull") {
    CaptureChord chord;
    CHECK_FALSE(chord.update(frameOf(true, 0.0f, 0.0f), kFrame).capture);
    CHECK(chord.update(frameOf(true, 0.0f, 1.0f), kFrame).capture);
    CHECK_FALSE(chord.update(frameOf(true, 0.0f, 1.0f), kFrame).capture); // still the same pull
    CHECK_FALSE(chord.update(frameOf(true, 0.0f, 0.0f), kFrame).capture);
    CHECK(chord.update(frameOf(true, 1.0f, 0.0f), kFrame).capture); // the other trigger works too
}

TEST_CASE("a trigger alone, or Menu alone, asks for nothing") {
    CaptureChord chord;
    CHECK_FALSE(chord.update(frameOf(false, 1.0f, 1.0f), kFrame).capture);
    CHECK_FALSE(chord.update(frameOf(false, 0.0f, 0.0f), kFrame).capture);
    const CaptureChordOutput menu = chord.update(frameOf(true, 0.0f, 0.0f), kFrame);
    CHECK_FALSE(menu.capture);
    CHECK_FALSE(menu.cancelMenu);
}

TEST_CASE("a trigger already down when Menu goes down is no capture, but is held back") {
    CaptureChord chord;
    const CaptureChordOutput firing = chord.update(frameOf(false, 0.0f, 1.0f), kFrame);
    CHECK_FALSE(firing.triggerHeldBack[1]);
    const CaptureChordOutput menu = chord.update(frameOf(true, 0.0f, 1.0f), kFrame);
    CHECK_FALSE(menu.capture);
    CHECK(menu.triggerHeldBack[1]);
}

TEST_CASE("a trigger pulled during Menu stays held back until it is let go") {
    CaptureChord chord;
    chord.update(frameOf(true, 0.0f, 0.0f), kFrame);
    CHECK(chord.update(frameOf(true, 0.0f, 1.0f), kFrame).triggerHeldBack[1]);
    const CaptureChordOutput menuUp = chord.update(frameOf(false, 0.0f, 1.0f), kFrame);
    CHECK(menuUp.triggerHeldBack[1]);
    CHECK_FALSE(menuUp.triggerHeldBack[0]);
    CHECK_FALSE(chord.update(frameOf(false, 0.0f, 0.0f), kFrame).triggerHeldBack[1]);
    CHECK_FALSE(
        chord.update(frameOf(false, 0.0f, 1.0f), kFrame).triggerHeldBack[1]); // an ordinary pull again
}

TEST_CASE("the Menu press with a capture is cancelled until Menu goes up") {
    CaptureChord chord;
    CHECK_FALSE(chord.update(frameOf(true, 0.0f, 0.0f), kFrame).cancelMenu);
    CHECK(chord.update(frameOf(true, 1.0f, 0.0f), kFrame).cancelMenu);
    CHECK(chord.update(frameOf(true, 0.0f, 0.0f), kFrame).cancelMenu);
    CHECK_FALSE(chord.update(frameOf(false, 0.0f, 0.0f), kFrame).cancelMenu);
    CHECK_FALSE(chord.update(frameOf(true, 0.0f, 0.0f), kFrame).cancelMenu); // the next press starts clean
}

TEST_CASE("a trigger resting below the press threshold does not capture") {
    CaptureChord chord;
    chord.update(frameOf(true, 0.0f, 0.0f), kFrame);
    CHECK_FALSE(chord.update(frameOf(true, 0.5f, 0.3f), kFrame).capture);
    CHECK(chord.update(frameOf(true, 0.6f, 0.3f), kFrame).capture);
}

namespace {

InputFrame sticksFrame(bool sticks, float leftTrigger, float rightTrigger) {
    InputFrame frame = frameOf(false, leftTrigger, rightTrigger);
    frame.left.stickClick = sticks;
    frame.right.stickClick = sticks;
    return frame;
}

CaptureChord sticksChord() {
    return CaptureChord(evr::input::kTriggerThresholds, CaptureButtons::MenuOrSticks);
}

// Both sticks held for `seconds` with the triggers up; returns the last output.
CaptureChordOutput holdSticks(CaptureChord& chord, float seconds, float rightTrigger = 0.0f) {
    CaptureChordOutput out;
    for (int i = 0; i < static_cast<int>(seconds / kFrame); ++i) {
        out = chord.update(sticksFrame(true, 0.0f, rightTrigger), kFrame);
    }
    return out;
}

} // namespace

TEST_CASE("Menu only: both sticks held do not chord") {
    CaptureChord chord;
    holdSticks(chord, 0.5f);
    const CaptureChordOutput pull = chord.update(sticksFrame(true, 0.0f, 1.0f), kFrame);
    CHECK_FALSE(pull.capture);
    CHECK_FALSE(pull.triggerHeldBack[1]);
    CHECK_FALSE(pull.cancelSticks);
}

TEST_CASE("Menu or sticks: both sticks held past the hold time + a trigger captures, the pull never fires") {
    CaptureChord chord = sticksChord();
    const CaptureChordOutput held = holdSticks(chord, 0.3f);
    CHECK_FALSE(held.capture);
    CHECK_FALSE(held.triggerHeldBack[1]); // the sticks alone hold nothing back
    const CaptureChordOutput pull = chord.update(sticksFrame(true, 0.0f, 1.0f), kFrame);
    CHECK(pull.capture);
    CHECK(pull.triggerHeldBack[1]);
    CHECK(pull.cancelSticks);
    CHECK_FALSE(pull.cancelMenu);
    CHECK(chord.update(sticksFrame(true, 0.0f, 1.0f), kFrame).cancelSticks); // until the chord ends
    // Sticks up first: the pull stays held back until the trigger is let go, and the cancel ends.
    const CaptureChordOutput up = chord.update(sticksFrame(false, 0.0f, 1.0f), kFrame);
    CHECK(up.triggerHeldBack[1]);
    CHECK_FALSE(up.cancelSticks);
    CHECK_FALSE(chord.update(sticksFrame(false, 0.0f, 0.0f), kFrame).triggerHeldBack[1]);
    CHECK_FALSE(chord.update(sticksFrame(false, 0.0f, 1.0f), kFrame).triggerHeldBack[1]); // firing again
}

TEST_CASE("Menu or sticks: a pull before the hold time, or with one stick, is an ordinary pull") {
    CaptureChord quick = sticksChord();
    holdSticks(quick, 0.1f);
    const CaptureChordOutput early = quick.update(sticksFrame(true, 0.0f, 1.0f), kFrame);
    CHECK_FALSE(early.capture);
    CHECK_FALSE(early.triggerHeldBack[1]);

    CaptureChord one = sticksChord();
    for (int i = 0; i < static_cast<int>(0.5f / kFrame); ++i) {
        InputFrame frame = sticksFrame(false, 0.0f, 0.0f);
        frame.left.stickClick = true;
        one.update(frame, kFrame);
    }
    InputFrame pull = sticksFrame(false, 0.0f, 1.0f);
    pull.left.stickClick = true;
    CHECK_FALSE(one.update(pull, kFrame).capture);
}

TEST_CASE("Menu or sticks: a trigger already down when the sticks go down keeps firing") {
    CaptureChord chord = sticksChord();
    chord.update(sticksFrame(false, 0.0f, 1.0f), kFrame);
    const CaptureChordOutput held = holdSticks(chord, 0.5f, 1.0f);
    CHECK_FALSE(held.capture);
    CHECK_FALSE(held.triggerHeldBack[1]);
    CHECK_FALSE(held.cancelSticks);
}

TEST_CASE("Menu or sticks: the Menu chord still works where the Menu button arrives") {
    CaptureChord chord = sticksChord();
    chord.update(frameOf(true, 0.0f, 0.0f), kFrame);
    CHECK(chord.update(frameOf(true, 0.0f, 0.0f), kFrame).triggerHeldBack[0]);
    const CaptureChordOutput pull = chord.update(frameOf(true, 1.0f, 0.0f), kFrame);
    CHECK(pull.capture);
    CHECK(pull.cancelMenu);
    CHECK_FALSE(pull.cancelSticks);
}
