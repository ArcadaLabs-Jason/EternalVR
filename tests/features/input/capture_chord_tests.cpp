#include "features/input/capture_chord.hpp"

#include "features/input/input_frames.hpp"

#include <doctest/doctest.h>

#include <ostream>

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
