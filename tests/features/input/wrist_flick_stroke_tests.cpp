// Roll wrist's strokes at their edges (features/input/wrist_flick.hpp, DECISIONS T-202): the over's and the
// back's sizes and the back's time limit just under and just over, a held hand's tremor, a wobble between or
// inside the strokes, and frames that come with no time.

#include "features/input/wrist_flick.hpp"

#include "features/input/wrist_flick_rig.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

using evr::input::kFlickBackDegrees;
using evr::input::kFlickBackSeconds;
using evr::input::kFlickOverDegrees;
using evr::input::kHoldTremorDegrees;
using evr::input::WristFlick;
using evr::test::wrist::flickOn;
using evr::test::wrist::handAt;
using evr::test::wrist::kFrame;
using evr::test::wrist::Rig;

TEST_CASE("strokes: an over just under 90 degrees is no flick, just over it is") {
    for (const float over : {kFlickOverDegrees - 3.0f, kFlickOverDegrees + 3.0f}) {
        CAPTURE(over);
        Rig rig;
        CHECK(rig.rollBy(over, 900.0f) == 0);
        CHECK(rig.rollBy(-90.0f, 900.0f) == (over > kFlickOverDegrees ? 1 : 0));
    }
}

TEST_CASE("strokes: a back just under 60 degrees is no flick, just over it is") {
    for (const float back : {kFlickBackDegrees - 3.0f, kFlickBackDegrees + 3.0f}) {
        CAPTURE(back);
        Rig rig;
        CHECK(rig.rollBy(120.0f, 900.0f) == 0);
        CHECK(rig.rollBy(-back, 900.0f) == (back > kFlickBackDegrees ? 1 : 0));
        CHECK(rig.run(0.0f, 0.5f) == 0);
    }
}

TEST_CASE("strokes: the back must come within 0.3 s of the over's furthest point") {
    // Held 20 frames (0.22 s) then back at 1500 degrees per second: 60 degrees after 24 frames (0.27 s). Held
    // 25 frames (0.28 s): 60 degrees after 29 frames (0.32 s), past the limit.
    for (const int held : {20, 25}) {
        CAPTURE(held);
        Rig rig;
        CHECK(rig.rollBy(120.0f, 1200.0f) == 0);
        CHECK(rig.run(0.0f, static_cast<float>(held) * kFrame) == 0);
        const bool inTime = static_cast<float>(held + 4) * kFrame <= kFlickBackSeconds;
        CHECK(rig.rollBy(-90.0f, 1500.0f) == (inTime ? 1 : 0));
    }
}

TEST_CASE("strokes: a hand turned over and held with a tremor is still held, and its late back is no flick") {
    // Over 150 degrees, then 0.4 s held with a 1 degree tremor at 8 Hz that creeps 0.8 degrees further, then
    // back fast. Each creep is a new furthest point by a fraction of a degree, under kHoldTremorDegrees.
    const auto run = [](float tremor) {
        Rig rig;
        int count = rig.rollBy(150.0f, 1200.0f);
        float previous = 0.0f;
        for (int i = 1; i <= 36; ++i) {
            const float t = static_cast<float>(i) * kFrame;
            const float now =
                tremor * std::sin(2.0f * std::numbers::pi_v<float> * 8.0f * t) + 0.8f * t / 0.4f;
            count += rig.stepBy(now - previous, 0.0f).flicked ? 1 : 0;
            previous = now;
        }
        return count + rig.rollBy(-120.0f, 1200.0f);
    };
    CHECK(kHoldTremorDegrees > 1.8f);
    CHECK(run(1.0f) == 0);
    CHECK(run(0.0f) == 0);
}

TEST_CASE("strokes: a wobble of 12 to 15 degrees at the turn does not lose the over") {
    for (const float wobble : {12.0f, 15.0f}) {
        CAPTURE(wobble);
        // Over 150, a quick wobble back and forward again, then the back: one flick.
        Rig rig;
        CHECK(rig.rollBy(150.0f, 1200.0f) == 0);
        CHECK(rig.rollBy(-wobble, 900.0f) == 0);
        CHECK(rig.rollBy(wobble, 900.0f) == 0);
        CHECK(rig.rollBy(-100.0f, 1200.0f) == 1);
        CHECK(rig.run(0.0f, 0.5f) == 0);
    }
}

TEST_CASE("strokes: a hesitation of 12 degrees inside the over does not split it") {
    // 60 over, 12 back, 60 on: one over of 108 degrees, then the back: one flick.
    Rig rig;
    CHECK(rig.rollBy(60.0f, 1200.0f) == 0);
    CHECK(rig.rollBy(-12.0f, 900.0f) == 0);
    CHECK(rig.rollBy(60.0f, 1200.0f) == 0);
    CHECK(rig.rollBy(-90.0f, 1200.0f) == 1);
}

TEST_CASE("strokes: the frame a flick starts on counts towards the over") {
    // From rest, the first frame turns 3.3 degrees at 300 degrees per second (its smoothed rate is still
    // under the quiet line), then 88 more: an over of 91 degrees, and the back flicks.
    Rig rig;
    CHECK(rig.run(0.0f, 0.3f) == 0);
    CHECK(rig.rollBy(300.0f * kFrame, 300.0f) == 0);
    CHECK(rig.rollBy(88.0f, 880.0f) == 0);
    CHECK(rig.rollBy(-90.0f, 900.0f) == 1);
}

TEST_CASE("strokes: a back that only passed the back's test is no over for the stroke after it") {
    // With no cooldown: an over of 120, then a back of 120 (110 at 1500 degrees per second, 10 slowing down)
    // with the aim swinging at 1000 all the way (the back's test only, never the over's): it flicks. Then,
    // quiet long enough to arm again, 80 fast the other way within 0.3 s of the back's end: that would be a
    // back only if the 120 had been an over too.
    const auto run = [](float swingWhileBack) {
        evr::input::FlickSettings settings = flickOn();
        settings.cooldownSeconds = 0.0f;
        Rig rig(settings);
        int count = rig.rollBy(120.0f, 1500.0f);
        count += rig.rollBy(-110.0f, 1500.0f, swingWhileBack);
        count += rig.rollBy(-10.0f, 300.0f, swingWhileBack);
        count += rig.run(0.0f, 22.0f * kFrame);
        return count + rig.rollBy(80.0f, 2000.0f);
    };
    CHECK(run(1000.0f) == 1);
    // Positive control: with the back a clean roll it is an over as well, and the 80 is its back.
    CHECK(run(0.0f) == 2);
}

TEST_CASE("strokes: a frame that comes with no time keeps its turn for the next one") {
    // No sample times; every other frame has a frame time of 0 (two reads of the mapper at once). The hand
    // turns over 160 degrees and back on every frame: none of the turn is lost, so it flicks once.
    WristFlick flick(flickOn());
    int count = 0;
    for (int i = 0; i <= 36; ++i) {
        const float roll = 800.0f * static_cast<float>(i <= 18 ? i : 36 - i) * kFrame;
        const float dt = i % 2 == 0 ? 2.0f * kFrame : 0.0f;
        count += flick.update(handAt(roll), evr::input::Hand::Right, dt).flicked ? 1 : 0;
    }
    CHECK(count == 1);
}
