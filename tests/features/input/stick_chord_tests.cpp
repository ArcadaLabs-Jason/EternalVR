#include "features/input/stick_chord.hpp"

#include <doctest/doctest.h>

using evr::input::StickChord;
using evr::input::StickChordOutput;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

struct Run {
    int leftPresses = 0;
    int rightPresses = 0;
    float recenterAt = -1.0f;
    bool leftDownFirstFrame = false;
};

// Each stick held over [from, to) seconds (never when from < 0).
Run run(float leftFrom, float leftTo, float rightFrom, float rightTo, float total) {
    StickChord chord;
    Run r;
    bool wasLeft = false;
    bool wasRight = false;
    const int frames = static_cast<int>(total / kFrame);
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) * kFrame;
        const bool left = leftFrom >= 0.0f && t >= leftFrom && t < leftTo;
        const bool right = rightFrom >= 0.0f && t >= rightFrom && t < rightTo;
        const StickChordOutput out = chord.update(left, right, kFrame);
        if (i == 0) {
            r.leftDownFirstFrame = out.click[0];
        }
        r.leftPresses += out.click[0] && !wasLeft ? 1 : 0;
        r.rightPresses += out.click[1] && !wasRight ? 1 : 0;
        wasLeft = out.click[0];
        wasRight = out.click[1];
        if (out.recenter && r.recenterAt < 0.0f) {
            r.recenterAt = t;
        }
    }
    return r;
}

} // namespace

TEST_CASE("stick chord: a single click is seen on its first frame") {
    const Run r = run(0.0f, 0.1f, -1.0f, 0.0f, 0.5f);
    CHECK(r.leftDownFirstFrame);
    CHECK(r.leftPresses == 1);
    CHECK(r.recenterAt < 0.0f);
}

TEST_CASE("stick chord: both pressed together: no clicks, recenter after the hold time") {
    const Run r = run(0.0f, 1.0f, 0.0f, 1.0f, 1.2f);
    CHECK(r.leftPresses == 0);
    CHECK(r.rightPresses == 0);
    CHECK(r.recenterAt == doctest::Approx(0.25f).epsilon(0.1));
}

TEST_CASE("stick chord: the second stick within the window joins without a click") {
    const Run r = run(0.0f, 1.0f, 0.12f, 1.0f, 1.2f);
    CHECK(r.leftPresses == 1); // the first click was already sent
    CHECK(r.rightPresses == 0);
    CHECK(r.recenterAt == doctest::Approx(0.12f + 0.25f).epsilon(0.1));
}

TEST_CASE("stick chord: a quick click while the other stick is held is sent, late, once") {
    const Run r = run(0.0f, 1.0f, 0.5f, 0.55f, 1.2f);
    CHECK(r.leftPresses == 1);
    CHECK(r.rightPresses == 1);
    CHECK(r.recenterAt < 0.0f);
}

TEST_CASE("stick chord: a stick held with the other past the window is the chord") {
    const Run r = run(0.0f, 2.0f, 0.5f, 2.0f, 2.2f);
    CHECK(r.leftPresses == 1);
    CHECK(r.rightPresses == 0);
    CHECK(r.recenterAt == doctest::Approx(0.75f).epsilon(0.1));
}

TEST_CASE("stick chord: letting one stick go ends the recenter; the other stays released until let go") {
    StickChord chord;
    StickChordOutput out;
    for (int i = 0; i < 40; ++i) {
        out = chord.update(true, true, kFrame);
    }
    CHECK(out.recenter);
    out = chord.update(true, false, kFrame);
    CHECK_FALSE(out.recenter);
    CHECK_FALSE(out.click[0]); // still part of the chord
    out = chord.update(false, false, kFrame);
    out = chord.update(true, false, kFrame);
    CHECK(out.click[0]); // a new press: instant again
}

TEST_CASE("stick chord: the other stick let go while one waits makes it an ordinary held click") {
    StickChord chord;
    for (int i = 0; i < 30; ++i) {
        chord.update(true, false, kFrame); // left held 0.33 s
    }
    StickChordOutput out = chord.update(true, true, kFrame); // right pressed: waits
    CHECK_FALSE(out.click[1]);
    out = chord.update(false, true, kFrame); // left let go inside the window
    CHECK(out.click[1]);
    CHECK_FALSE(out.recenter);
}
