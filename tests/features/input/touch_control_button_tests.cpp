// A touch on a control bound like a button (touch_button.hpp, TouchKind::Control): the thumb rest's filter,
// tap and hold timed by the touch as sensed, and a touch that goes on to use its own control is no press.

#include "features/input/rest_touch_filter.hpp"
#include "features/input/touch_button.hpp"

#include <doctest/doctest.h>

#include <vector>

using evr::input::kOwnButtonVoidSeconds;
using evr::input::kRestDebounceSeconds;
using evr::input::TouchButton;
using evr::input::TouchButtonOutput;
using evr::input::TouchButtonSample;
using evr::input::TouchKind;

namespace {

constexpr float kFrame = 1.0f / 90.0f;
constexpr float kHold = 0.25f;
constexpr float kHoldOver = 0.15f;

int frames(float seconds) {
    return static_cast<int>(seconds / kFrame + 0.5f);
}

TouchButton control() {
    return TouchButton(kHold, kHoldOver, TouchKind::Control);
}

// `own`: the control under the touch itself in use (so one of its own controls too); `other`: only another
// of its own controls (another control under the same thumb).
TouchButtonSample touch(bool on, bool own = false, bool other = false) {
    TouchButtonSample s;
    s.raw = on;
    s.ownControl = own || other;
    s.ownLanding = own;
    return s;
}

struct Run {
    int taps = 0;
    int holdFrames = 0;
    int downFrames = 0;
    int presses = 0; // times down went down
    bool wasDown = false;
};

void feed(TouchButton& b, const TouchButtonSample& s, int count, Run& run) {
    for (int i = 0; i < count; ++i) {
        const TouchButtonOutput o = b.update(s, kFrame);
        run.taps += o.tap ? 1 : 0;
        run.holdFrames += o.hold ? 1 : 0;
        run.downFrames += o.down ? 1 : 0;
        run.presses += o.down && !run.wasDown ? 1 : 0;
        run.wasDown = o.down;
    }
}

} // namespace

TEST_CASE("a touch on a control taps when touched and lifted, holds when kept, presses while on") {
    TouchButton b = control();
    CHECK(b.kind() == TouchKind::Control);
    Run run;
    feed(b, touch(false), 3, run);
    feed(b, touch(true), frames(0.12f), run);
    CHECK(run.taps == 0);
    CHECK(run.downFrames == frames(0.12f)); // down from the first frame
    feed(b, touch(false), frames(0.3f), run);
    CHECK(run.taps == 1);
    CHECK(run.holdFrames == 0);
    CHECK(b.stats().taps == 1);

    Run kept;
    feed(b, touch(true), frames(0.5f), kept);
    feed(b, touch(false), frames(0.3f), kept);
    CHECK(kept.taps == 0);
    CHECK(kept.holdFrames > 0);
    CHECK(kept.presses == 1);
    CHECK(b.stats().holds == 1);
}

TEST_CASE("a touch that goes on to press its control ends at once and is no tap") {
    TouchButton b = control();
    Run run;
    feed(b, touch(false), 3, run);
    // The thumb lands on A, then presses it (a jump): the press binding ends on that frame.
    feed(b, touch(true), frames(0.08f), run);
    const int before = run.downFrames;
    CHECK(before == frames(0.08f));
    feed(b, touch(true, true), frames(0.1f), run);
    CHECK(run.downFrames == before);
    // Let go of A, the thumb still on it a while, then off: no tap, no hold, no new press.
    feed(b, touch(true), frames(0.4f), run);
    feed(b, touch(false), frames(0.3f), run);
    CHECK(run.taps == 0);
    CHECK(run.holdFrames == 0);
    CHECK(run.presses == 1);
    CHECK(b.stats().ownUse == 1);

    // The same touch without the press is a tap (positive control for the case above).
    TouchButton plain = control();
    Run tap;
    feed(plain, touch(true), frames(0.08f), tap);
    feed(plain, touch(false), frames(0.3f), tap);
    CHECK(tap.taps == 1);
}

TEST_CASE("its own control used during the hold-over still ends the touch: no tap") {
    TouchButton b = control();
    Run run;
    feed(b, touch(true), frames(0.1f), run);
    // The touch sensor drops for a frame (a gap) while the stick is pushed out: no tap on the lift.
    feed(b, touch(false, true), 1, run);
    feed(b, touch(false), frames(0.3f), run);
    CHECK(run.taps == 0);
}

TEST_CASE("a hold ends when its own control is used") {
    TouchButton b = control();
    Run run;
    feed(b, touch(true), frames(0.4f), run);
    CHECK(run.holdFrames > 0);
    const int held = run.holdFrames;
    feed(b, touch(true, true), frames(0.2f), run);
    CHECK(run.holdFrames == held);
    feed(b, touch(true), frames(0.5f), run);
    CHECK(run.holdFrames == held); // not again until the touch lifts and lands again
    feed(b, touch(false), frames(0.5f), run);
    feed(b, touch(true), frames(0.4f), run);
    CHECK(run.holdFrames > held);
}

TEST_CASE("a landing straight after its own control was used presses nothing") {
    TouchButton b = control();
    Run run;
    // A pressed and let go, the thumb off A for a moment, then back on it within the void.
    feed(b, touch(true, true), frames(0.1f), run);
    feed(b, touch(false), frames(0.3f), run);
    REQUIRE(frames(0.3f) * kFrame < kOwnButtonVoidSeconds);
    Run back;
    feed(b, touch(true), frames(0.12f), back);
    feed(b, touch(false), frames(0.3f), back);
    CHECK(back.downFrames == 0);
    CHECK(back.taps == 0);
    CHECK(b.stats().voided >= 1);

    // Past the void the next touch counts again.
    feed(b, touch(false), frames(0.5f), back);
    feed(b, touch(true), frames(0.12f), back);
    feed(b, touch(false), frames(0.3f), back);
    CHECK(back.taps == 1);
}

TEST_CASE("a brush shorter than the debounce is no tap; a flicker is one touch") {
    TouchButton b = control();
    Run run;
    feed(b, touch(true), 1, run);
    feed(b, touch(false), frames(0.3f), run);
    REQUIRE(kFrame < kRestDebounceSeconds);
    CHECK(run.taps == 0);
    CHECK(b.stats().brushes == 1);

    // On, off for two frames, on again: one press and one tap.
    TouchButton f = control();
    Run flicker;
    feed(f, touch(true), frames(0.05f), flicker);
    feed(f, touch(false), 2, flicker);
    feed(f, touch(true), frames(0.05f), flicker);
    feed(f, touch(false), frames(0.3f), flicker);
    CHECK(flicker.presses == 1);
    CHECK(flicker.taps == 1);
    CHECK(f.stats().gapsForgiven == 1);
}

TEST_CASE("every touch on a control past the debounce is a tap or a hold, at common refresh rates") {
    for (const float hz : {72.0f, 90.0f, 120.0f}) {
        const float dt = 1.0f / hz;
        // From the first length past the debounce (a shorter touch is a brush: neither).
        for (int n = static_cast<int>(kRestDebounceSeconds * hz) + 1; n < static_cast<int>(0.6f * hz); ++n) {
            TouchButton b = control();
            int taps = 0;
            int holds = 0;
            for (int i = 0; i < n; ++i) {
                holds += b.update(touch(true), dt).hold ? 1 : 0;
            }
            for (int i = 0; i < static_cast<int>(0.4f * hz); ++i) {
                taps += b.update(touch(false), dt).tap ? 1 : 0;
            }
            CAPTURE(hz);
            CAPTURE(n);
            CHECK((taps == 1) != (holds > 0));
        }
    }
}

TEST_CASE("a touch on a control ignores the rest's elsewhere evidence") {
    // The rest's hard evidence (a face button pressed, the stick out) holds a rest's landing back; a touch on
    // a control reads only its own control.
    TouchButton b = control();
    TouchButtonSample s = touch(true);
    s.faceButton = true;
    s.stickOut = true;
    s.vetoTouch = true;
    Run run;
    feed(b, s, frames(0.1f), run);
    CHECK(run.downFrames == frames(0.1f));
    // The same sample on a rest waits (positive control).
    TouchButton rest(kHold, kHoldOver, TouchKind::Rest);
    Run restRun;
    feed(rest, s, 2, restRun);
    CHECK(restRun.downFrames == 0);
}

TEST_CASE("a touch on a control still on when a menu lets go fires only after a fresh landing") {
    TouchButton b = control();
    Run run;
    feed(b, touch(true), frames(0.1f), run);
    b.cancel();
    feed(b, touch(true), frames(0.1f), run);
    const int before = run.downFrames;
    feed(b, touch(false), frames(0.3f), run);
    CHECK(run.taps == 0);
    feed(b, touch(true), frames(0.1f), run);
    CHECK(run.downFrames > before);
}

TEST_CASE("another control under the thumb ends a touch but does not void the next landing") {
    TouchButton b = control();
    Run run;
    // Touched, then another control under the thumb used: the touch ends, no tap.
    feed(b, touch(true), frames(0.1f), run);
    feed(b, touch(true, false, true), frames(0.1f), run);
    feed(b, touch(false), frames(0.3f), run);
    CHECK(run.taps == 0);
    CHECK(b.stats().ownUse == 1);
    // That control let go, the touch lands 0.15 s later: it counts.
    feed(b, touch(false), frames(0.15f), run);
    feed(b, touch(true), frames(0.12f), run);
    feed(b, touch(false), frames(0.3f), run);
    CHECK(run.taps == 1);
    CHECK(b.stats().voided == 0);
}
