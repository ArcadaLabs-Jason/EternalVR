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
    CHECK_FALSE(chord.update(frameOf(true, 0.0f, 0.0f)).capture);
    CHECK(chord.update(frameOf(true, 0.0f, 1.0f)).capture);
    CHECK_FALSE(chord.update(frameOf(true, 0.0f, 1.0f)).capture); // still the same pull
    CHECK_FALSE(chord.update(frameOf(true, 0.0f, 0.0f)).capture);
    CHECK(chord.update(frameOf(true, 1.0f, 0.0f)).capture); // the other trigger works too
}

TEST_CASE("a trigger alone, or Menu alone, asks for nothing") {
    CaptureChord chord;
    CHECK_FALSE(chord.update(frameOf(false, 1.0f, 1.0f)).capture);
    CHECK_FALSE(chord.update(frameOf(false, 0.0f, 0.0f)).capture);
    const CaptureChordOutput menu = chord.update(frameOf(true, 0.0f, 0.0f));
    CHECK_FALSE(menu.capture);
    CHECK_FALSE(menu.cancelMenu);
}

TEST_CASE("a trigger already down when Menu goes down is no capture, but is held back") {
    CaptureChord chord;
    const CaptureChordOutput firing = chord.update(frameOf(false, 0.0f, 1.0f));
    CHECK_FALSE(firing.triggerHeldBack[1]);
    const CaptureChordOutput menu = chord.update(frameOf(true, 0.0f, 1.0f));
    CHECK_FALSE(menu.capture);
    CHECK(menu.triggerHeldBack[1]);
}

TEST_CASE("a trigger pulled during Menu stays held back until it is let go") {
    CaptureChord chord;
    chord.update(frameOf(true, 0.0f, 0.0f));
    CHECK(chord.update(frameOf(true, 0.0f, 1.0f)).triggerHeldBack[1]);
    const CaptureChordOutput menuUp = chord.update(frameOf(false, 0.0f, 1.0f));
    CHECK(menuUp.triggerHeldBack[1]);
    CHECK_FALSE(menuUp.triggerHeldBack[0]);
    CHECK_FALSE(chord.update(frameOf(false, 0.0f, 0.0f)).triggerHeldBack[1]);
    CHECK_FALSE(chord.update(frameOf(false, 0.0f, 1.0f)).triggerHeldBack[1]); // an ordinary pull again
}

TEST_CASE("the Menu press with a capture is cancelled until Menu goes up") {
    CaptureChord chord;
    CHECK_FALSE(chord.update(frameOf(true, 0.0f, 0.0f)).cancelMenu);
    CHECK(chord.update(frameOf(true, 1.0f, 0.0f)).cancelMenu);
    CHECK(chord.update(frameOf(true, 0.0f, 0.0f)).cancelMenu);
    CHECK_FALSE(chord.update(frameOf(false, 0.0f, 0.0f)).cancelMenu);
    CHECK_FALSE(chord.update(frameOf(true, 0.0f, 0.0f)).cancelMenu); // the next press starts clean
}

TEST_CASE("a trigger resting below the press threshold does not capture") {
    CaptureChord chord;
    chord.update(frameOf(true, 0.0f, 0.0f));
    CHECK_FALSE(chord.update(frameOf(true, 0.5f, 0.3f)).capture);
    CHECK(chord.update(frameOf(true, 0.6f, 0.3f)).capture);
}

namespace {

InputFrame secondaryFrame(bool y, float leftTrigger, float rightTrigger) {
    InputFrame frame = frameOf(false, leftTrigger, rightTrigger);
    frame.left.secondaryButton = y;
    return frame;
}

} // namespace

TEST_CASE("Menu only: the secondary button does not chord") {
    CaptureChord chord;
    chord.update(secondaryFrame(true, 0.0f, 0.0f));
    const CaptureChordOutput pull = chord.update(secondaryFrame(true, 0.0f, 1.0f));
    CHECK_FALSE(pull.capture);
    CHECK_FALSE(pull.triggerHeldBack[1]);
    CHECK_FALSE(pull.cancelSecondary);
}

TEST_CASE("SteamVR: the secondary button held + a trigger pulled captures, and the pull never fires") {
    CaptureChord chord(evr::input::kTriggerThresholds, CaptureButtons::MenuOrSecondary);
    const CaptureChordOutput held = chord.update(secondaryFrame(true, 0.0f, 0.0f));
    CHECK_FALSE(held.capture);
    CHECK_FALSE(held.triggerHeldBack[1]); // Y alone holds nothing back
    const CaptureChordOutput pull = chord.update(secondaryFrame(true, 0.0f, 1.0f));
    CHECK(pull.capture);
    CHECK(pull.triggerHeldBack[1]);
    CHECK(pull.cancelSecondary);
    CHECK_FALSE(pull.cancelMenu);
    // Y up first: the pull stays held back until the trigger is let go, and the cancel ends with the press.
    const CaptureChordOutput yUp = chord.update(secondaryFrame(false, 0.0f, 1.0f));
    CHECK(yUp.triggerHeldBack[1]);
    CHECK_FALSE(yUp.cancelSecondary);
    CHECK_FALSE(chord.update(secondaryFrame(false, 0.0f, 0.0f)).triggerHeldBack[1]);
    CHECK_FALSE(chord.update(secondaryFrame(false, 0.0f, 1.0f)).triggerHeldBack[1]); // firing again
}

TEST_CASE("SteamVR: a Y tap while firing keeps firing and captures nothing") {
    CaptureChord chord(evr::input::kTriggerThresholds, CaptureButtons::MenuOrSecondary);
    chord.update(secondaryFrame(false, 0.0f, 1.0f));
    const CaptureChordOutput tap = chord.update(secondaryFrame(true, 0.0f, 1.0f));
    CHECK_FALSE(tap.capture);
    CHECK_FALSE(tap.triggerHeldBack[1]);
    CHECK_FALSE(tap.cancelSecondary);
    CHECK_FALSE(chord.update(secondaryFrame(false, 0.0f, 1.0f)).triggerHeldBack[1]);
}

TEST_CASE("SteamVR: the Menu chord still works where the Menu button arrives") {
    CaptureChord chord(evr::input::kTriggerThresholds, CaptureButtons::MenuOrSecondary);
    chord.update(frameOf(true, 0.0f, 0.0f));
    CHECK(chord.update(frameOf(true, 0.0f, 0.0f)).triggerHeldBack[0]);
    const CaptureChordOutput pull = chord.update(frameOf(true, 1.0f, 0.0f));
    CHECK(pull.capture);
    CHECK(pull.cancelMenu);
    CHECK_FALSE(pull.cancelSecondary);
}
