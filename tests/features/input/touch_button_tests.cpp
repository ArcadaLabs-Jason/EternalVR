// A thumb rest bound like a button (touch_button.hpp): the rest's touch as the thumb-rest wheel filters it,
// tap and hold timed by the touch as sensed, and no press from a thumb straight off its own controls.

#include "features/input/tap_hold.hpp"
#include "features/input/touch_button.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <vector>

using evr::input::kRestDebounceSeconds;
using evr::input::TapHoldDetector;
using evr::input::TouchButton;
using evr::input::TouchButtonOutput;
using evr::input::TouchButtonSample;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

int frames(float seconds) {
    return static_cast<int>(seconds / kFrame + 0.5f);
}

TouchButtonSample rest(bool on) {
    TouchButtonSample s;
    s.raw = on;
    return s;
}

struct Run {
    std::vector<TouchButtonOutput> out;

    [[nodiscard]] int taps() const {
        int n = 0;
        for (const TouchButtonOutput& o : out) {
            n += o.tap ? 1 : 0;
        }
        return n;
    }
    [[nodiscard]] bool anyHold() const {
        for (const TouchButtonOutput& o : out) {
            if (o.hold) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] bool anyDown() const {
        for (const TouchButtonOutput& o : out) {
            if (o.down) {
                return true;
            }
        }
        return false;
    }
    // Times `down` went down.
    [[nodiscard]] int presses() const {
        int n = 0;
        bool was = false;
        for (const TouchButtonOutput& o : out) {
            n += o.down && !was ? 1 : 0;
            was = o.down;
        }
        return n;
    }
};

void feed(TouchButton& b, const TouchButtonSample& s, int count, Run& run) {
    for (int i = 0; i < count; ++i) {
        run.out.push_back(b.update(s, kFrame));
    }
}

} // namespace

TEST_CASE("a rest that flickers off for one to three frames is one press") {
    TouchButton b;
    Run run;
    // 0.6 s on, with dropouts of 1, 2 and 3 frames every few frames.
    const std::vector<int> dropouts{1, 2, 3, 1, 3, 2, 1, 2};
    for (const int gap : dropouts) {
        feed(b, rest(true), 5, run);
        feed(b, rest(false), gap, run);
    }
    feed(b, rest(true), 5, run);
    CHECK(run.out.front().down); // the first frame
    feed(b, rest(false), frames(0.3f), run);
    CHECK(run.presses() == 1);
    CHECK_FALSE(run.out.back().down);
    CHECK(b.stats().gapsForgiven == dropouts.size());
    CHECK(b.stats().touches == dropouts.size() + 1);
}

TEST_CASE("a real 0.15 s touch is a tap, not a hold") {
    TouchButton b;
    Run run;
    feed(b, rest(true), frames(0.15f), run);
    feed(b, rest(false), frames(0.4f), run);
    CHECK(run.taps() == 1);
    CHECK_FALSE(run.anyHold());
    // The press lasts until the hold-over has lapsed: past the hold time, so a TapHoldDetector on it would
    // have made this touch a hold and no tap.
    TapHoldDetector detector;
    bool held = false;
    bool tapped = false;
    for (const TouchButtonOutput& o : run.out) {
        const auto t = detector.update(o.down, kFrame);
        held = held || t.hold;
        tapped = tapped || t.tap;
    }
    CHECK(held);
    CHECK_FALSE(tapped);
    // The tap fires on the lift, a hold-over after the touch went.
    int lift = 0;
    while (run.out[static_cast<std::size_t>(lift)].down) {
        ++lift;
    }
    CHECK(run.out[static_cast<std::size_t>(lift)].tap);
    CHECK(lift >= frames(0.15f) + frames(0.15f) - 1);
}

TEST_CASE("a touch held past the hold time is a hold and no tap") {
    TouchButton b;
    Run run;
    feed(b, rest(true), frames(0.24f), run);
    CHECK_FALSE(run.anyHold());
    feed(b, rest(true), frames(0.1f), run);
    CHECK(run.out.back().hold);
    CHECK(run.out.back().down);
    feed(b, rest(false), frames(0.4f), run);
    CHECK(run.taps() == 0);
    CHECK(b.stats().holds == 1);
}

TEST_CASE("a hold fires only with the sensor on: it waits through a gap") {
    TouchButton b;
    Run run;
    feed(b, rest(true), frames(0.2f), run);
    // A gap across the hold time, forgiven (shorter than the hold-over).
    feed(b, rest(false), frames(0.1f), run);
    CHECK_FALSE(run.anyHold());
    CHECK(run.out.back().down);
    // The touch back: the hold fires on that frame.
    feed(b, rest(true), 1, run);
    CHECK(run.out.back().hold);

    // A gap across the hold time that lifts: no hold, and the touch as sensed (0.2 s) was a tap.
    TouchButton lapsed;
    Run second;
    feed(lapsed, rest(true), frames(0.2f), second);
    feed(lapsed, rest(false), frames(0.3f), second);
    CHECK_FALSE(second.anyHold());
    CHECK(second.taps() == 1);
}

TEST_CASE("pressing A ends a press at once") {
    TouchButton b;
    Run run;
    feed(b, rest(true), frames(0.5f), run);
    TouchButtonSample pressing = rest(true);
    pressing.faceButton = true;
    feed(b, pressing, 2, run);
    CHECK(run.out.back().down); // the touch still sensed: nothing changes
    TouchButtonSample gone;
    gone.faceButton = true;
    feed(b, gone, 1, run);
    CHECK_FALSE(run.out.back().down);
    CHECK(b.stats().cutLifts == 1);

    // Without the thumb going elsewhere the touch is held for the hold-over.
    TouchButton plain;
    Run held;
    feed(plain, rest(true), frames(0.5f), held);
    feed(plain, rest(false), 1, held);
    CHECK(held.out.back().down);
}

TEST_CASE("a landing begun with the thumb on its stick waits") {
    TouchButton b;
    Run run;
    TouchButtonSample both = rest(true);
    both.vetoTouch = true;
    feed(b, both, 3, run);
    CHECK_FALSE(run.anyDown());
    CHECK(b.stats().begunElsewhere == 1);
    // Registered once the contact has lasted the debounce.
    feed(b, both, frames(0.06f), run);
    CHECK(run.out.back().down);

    // ... or at once when the thumb leaves the stick.
    TouchButton left;
    Run second;
    feed(left, both, 2, second);
    CHECK_FALSE(second.anyDown());
    feed(left, rest(true), 1, second);
    CHECK(second.out.back().down);
}

TEST_CASE("a thumb straight off its own controls presses nothing until it lands again") {
    SUBCASE("after a face button") {
        TouchButton b;
        Run run;
        TouchButtonSample jump;
        jump.faceButton = true;
        feed(b, jump, frames(0.1f), run);
        feed(b, rest(false), frames(0.2f), run);
        feed(b, rest(true), frames(0.5f), run);
        feed(b, rest(false), frames(0.3f), run);
        CHECK_FALSE(run.anyDown());
        CHECK_FALSE(run.anyHold());
        CHECK(run.taps() == 0);
        CHECK(b.stats().voided == 1);
        feed(b, rest(true), 2, run);
        CHECK(run.out.back().down);
    }
    SUBCASE("after its own stick") {
        TouchButton b;
        Run run;
        TouchButtonSample turning;
        turning.stickOut = true;
        feed(b, turning, frames(0.2f), run);
        feed(b, rest(false), frames(0.3f), run);
        feed(b, rest(true), frames(0.1f), run);
        feed(b, rest(false), frames(0.3f), run);
        CHECK_FALSE(run.anyDown());
        CHECK(run.taps() == 0);
        feed(b, rest(true), 2, run);
        CHECK(run.out.back().down);
    }
    SUBCASE("long enough after them, a press") {
        TouchButton b;
        Run run;
        TouchButtonSample jump;
        jump.faceButton = true;
        feed(b, jump, frames(0.1f), run);
        feed(b, rest(false), frames(0.45f), run);
        feed(b, rest(true), 2, run);
        CHECK(run.out.back().down);
        CHECK(b.stats().voided == 0);
    }
}

TEST_CASE("a stuck touch sensor beside the rest stops holding landings back") {
    TouchButton b;
    Run run;
    TouchButtonSample brush = rest(true);
    brush.vetoTouch = true;
    TouchButtonSample off;
    off.vetoTouch = true;
    // The stick's touch on all along; the rest comes for 0.03 s every 0.2 s, too short to register.
    for (int i = 0; i < 7; ++i) {
        feed(b, brush, frames(0.03f), run);
        feed(b, off, frames(0.2f), run);
    }
    CHECK_FALSE(run.anyDown());
    CHECK(b.stats().begunElsewhere > 0);
    // 1.6 s in: the stick's touch is taken as seeing the resting thumb, so the touch lands at once.
    feed(b, brush, 1, run);
    CHECK(run.out.back().down);
}

TEST_CASE("a cancelled touch presses nothing until it lands again") {
    TouchButton b;
    Run run;
    feed(b, rest(true), 3, run);
    CHECK(run.out.back().down);
    b.cancel();
    feed(b, rest(true), frames(0.5f), run);
    CHECK_FALSE(run.out.back().down);
    CHECK_FALSE(run.out.back().hold);
    feed(b, rest(false), frames(0.3f), run);
    CHECK(run.taps() == 0);
    feed(b, rest(true), 1, run);
    CHECK(run.out.back().down);

    // A cancel with nothing touched applies to no later touch.
    TouchButton idle;
    Run second;
    idle.cancel();
    feed(idle, rest(false), 3, second);
    feed(idle, rest(true), 1, second);
    CHECK(second.out.back().down);
}

TEST_CASE("every touch is a tap or a hold, whatever its length in frames") {
    // At 90 Hz and the default 0.25 s hold time a 23-frame touch is sensed 23 frames (0.256 s, past the hold
    // time) yet fires its hold only from the 24th frame on: it has to be a tap.
    for (const float hz : {72.0f, 90.0f, 120.0f}) {
        const float dt = 1.0f / hz;
        for (int n = 1; n <= static_cast<int>(0.5f * hz); ++n) {
            CAPTURE(hz);
            CAPTURE(n);
            TouchButton b;
            int taps = 0;
            int holds = 0;
            bool held = false;
            for (int i = 0; i < n + static_cast<int>(0.4f * hz); ++i) {
                const TouchButtonOutput o = b.update(rest(i < n), dt);
                taps += o.tap ? 1 : 0;
                holds += o.hold && !held ? 1 : 0;
                held = o.hold;
            }
            CHECK(taps + holds == 1);
            CHECK(b.stats().taps + b.stats().holds == 1);
        }
    }
}

TEST_CASE("a thumb passing over the rest on its way to a button or its stick taps nothing") {
    SUBCASE("to A") {
        TouchButton b;
        Run run;
        feed(b, rest(true), 3, run);
        CHECK(run.out.back().down); // a press still begins on the first frame
        TouchButtonSample pressing;
        pressing.faceButton = true;
        feed(b, pressing, frames(0.3f), run);
        feed(b, rest(false), frames(0.3f), run);
        CHECK(run.taps() == 0);
        CHECK(b.stats().cutLifts == 1);
        CHECK(b.stats().passedOver == 1);
    }
    SUBCASE("to its stick, seen by the stick's touch") {
        TouchButton b;
        Run run;
        feed(b, rest(true), 3, run);
        TouchButtonSample onStick;
        onStick.vetoTouch = true;
        feed(b, onStick, frames(0.3f), run);
        feed(b, rest(false), frames(0.3f), run);
        CHECK(run.taps() == 0);
        CHECK(b.stats().cutLifts == 1);
        CHECK(b.stats().passedOver == 1);
    }
    SUBCASE("a touch sensed for the debounce and then cut is a tap") {
        TouchButton b;
        Run run;
        feed(b, rest(true), frames(kRestDebounceSeconds) + 1, run);
        TouchButtonSample pressing;
        pressing.faceButton = true;
        feed(b, pressing, frames(0.3f), run);
        CHECK(run.taps() == 1);
        CHECK(b.stats().cutLifts == 1);
        CHECK(b.stats().passedOver == 0);
    }
    SUBCASE("as brief a touch lifted plainly is a tap") {
        TouchButton b;
        Run run;
        feed(b, rest(true), 3, run);
        feed(b, rest(false), frames(0.3f), run);
        CHECK(run.taps() == 1);
        CHECK(b.stats().cutLifts == 0);
        CHECK(b.stats().passedOver == 0);
    }
}
