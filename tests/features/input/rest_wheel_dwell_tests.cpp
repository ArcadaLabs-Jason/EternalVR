// The thumb-rest wheel's dwell (DECISIONS T-223): a touch counts as a resting thumb once it has stayed on for
// kRestDwellSeconds. Under edge the window opens at the end of it; under full a stick already out is left to
// the game until then.

#include "features/input/rest_wheel.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>

using evr::input::Axis2;
using evr::input::Hand;
using evr::input::kRestDwellSeconds;
using evr::input::RestWheel;
using evr::input::RestWheelEvent;
using evr::input::RestWheelFrame;
using evr::input::RestWheelHands;
using evr::input::RestWheelMode;
using evr::input::RestWheelOutput;
using evr::input::RestWheelPick;
using evr::input::RestWheelSettings;
using evr::input::RestWheelVoid;

namespace {

constexpr float kFrame = 1.0f / 90.0f;
constexpr Axis2 kCentre{};
constexpr Axis2 kUp{0.0f, 1.0f};
constexpr Axis2 kDown{0.0f, -1.0f};
constexpr Axis2 kRight{1.0f, 0.0f};
constexpr int kRightHand = 1;

RestWheel wheel(RestWheelMode mode, RestWheelPick pick = RestWheelPick::Wheel) {
    RestWheelHands hands;
    hands.hasRest = {true, true};
    hands.moveStick = Hand::Left;
    hands.turnStick = Hand::Right;
    RestWheelSettings settings;
    settings.mode = mode;
    settings.pick = pick;
    return RestWheel(settings, hands);
}

// The left thumb on or off its rest, the right stick as given, the left stick centred.
RestWheelFrame frame(bool rest, Axis2 right) {
    RestWheelFrame f;
    f.rest = {rest, false};
    f.sticks = {kCentre, right};
    return f;
}

int frames(float seconds) {
    return static_cast<int>(std::lround(seconds / kFrame));
}

struct Run {
    bool armed = false;
    bool opened = false;
    int taken = 0; // frames the right stick was the wheel's
    int count = 0;
    RestWheelVoid voided = RestWheelVoid::None;
    float sinceTouch = 0.0f; // of the last arm
};

Run feed(RestWheel& w, const RestWheelFrame& f, int count, Run run = {}) {
    for (int i = 0; i < count; ++i) {
        const RestWheelOutput out = w.update(f, kFrame);
        run.armed = run.armed || out.event == RestWheelEvent::Armed;
        run.opened = run.opened || out.event == RestWheelEvent::Opened;
        if (out.event == RestWheelEvent::Voided) {
            run.voided = out.voided;
        }
        if (out.event == RestWheelEvent::Armed) {
            run.sinceTouch = out.sinceTouch;
        }
        run.taken += out.taken[kRightHand] ? 1 : 0;
        ++run.count;
    }
    return run;
}

// The thumb lands with both sticks centred and rests for `rested` seconds; then the right stick is pushed to
// `push` and held for `held` seconds.
Run restThenPush(RestWheelMode mode, float rested, Axis2 push, float held = 1.0f) {
    RestWheel w = wheel(mode);
    feed(w, frame(true, kCentre), frames(rested));
    return feed(w, frame(true, push), frames(held));
}

} // namespace

TEST_CASE("edge: a brush at a standstill followed by walking off opens no wheel") {
    // Each of these pushes began before the touch had stayed on for the dwell. Without a dwell the window is
    // open from the landing and every one of them arms (the positive control).
    for (const float rested : {0.01f, 0.05f, 0.1f, 0.15f, 0.2f}) {
        for (const Axis2 push : {kUp, kDown}) {
            CAPTURE(rested);
            const Run walk = restThenPush(RestWheelMode::Edge, rested, push);
            CHECK_FALSE(walk.armed);
            CHECK_FALSE(walk.opened);
            CHECK(walk.taken == 0);
        }
    }
}

TEST_CASE("edge: a rest of the dwell, then a push, still opens the wheel, inside the window only") {
    const float window = evr::input::kEdgeWindowSeconds;
    for (const Axis2 push : {kUp, kDown, kRight}) {
        const Run run = restThenPush(RestWheelMode::Edge, kRestDwellSeconds + 0.05f, push, 0.3f);
        CHECK(run.armed);
        CHECK(run.opened);
    }
    // The window is the setting, counted from the end of the dwell: open just inside it, shut just past it.
    CHECK(restThenPush(RestWheelMode::Edge, kRestDwellSeconds + window - 0.05f, kUp, 0.3f).armed);
    CHECK_FALSE(restThenPush(RestWheelMode::Edge, kRestDwellSeconds + window + 0.05f, kUp, 0.3f).armed);
    // Not before the dwell is over, to the frame.
    CHECK_FALSE(restThenPush(RestWheelMode::Edge, kRestDwellSeconds - 2.0f * kFrame, kUp, 0.3f).armed);
    CHECK(restThenPush(RestWheelMode::Edge, kRestDwellSeconds + 2.0f * kFrame, kUp, 0.3f).armed);
}

TEST_CASE("edge: a push already under way when the dwell ends is a walk, not a pick") {
    RestWheel w = wheel(RestWheelMode::Edge);
    feed(w, frame(true, kCentre), frames(0.1f));
    const Run walk = feed(w, frame(true, kUp), frames(1.0f));
    CHECK_FALSE(walk.armed);
    CHECK(walk.taken == 0);
}

TEST_CASE("edge: a push during the dwell uses the landing up: stopping and walking again opens no wheel") {
    RestWheel w = wheel(RestWheelMode::Edge);
    feed(w, frame(true, kCentre), frames(0.05f));
    // Walking off, stopping a moment later and walking again, the thumb on the rest throughout: both pushes
    // are inside the window that would open at the end of the dwell.
    Run walk = feed(w, frame(true, kUp), frames(0.15f));
    walk = feed(w, frame(true, kCentre), frames(0.1f), walk);
    walk = feed(w, frame(true, kUp), frames(0.5f), walk);
    CHECK_FALSE(walk.armed);
    CHECK(walk.taken == 0);
    // The thumb lifted and landed afresh: a new landing, a new dwell.
    feed(w, frame(false, kCentre), frames(0.3f));
    feed(w, frame(true, kCentre), frames(kRestDwellSeconds + 0.05f));
    CHECK(feed(w, frame(true, kUp), frames(0.2f)).armed);
}

TEST_CASE("edge: picks in a row do not wait for the dwell again") {
    RestWheel w = wheel(RestWheelMode::Edge);
    feed(w, frame(true, kCentre), frames(kRestDwellSeconds + 0.1f));
    const Run first = feed(w, frame(true, kUp), frames(0.4f));
    REQUIRE(first.opened);
    feed(w, frame(true, kCentre), frames(0.4f));
    // The thumb has been resting throughout: the next push, once the stick has been centred for the re-arm
    // gap, picks.
    CHECK(feed(w, frame(true, kRight), 2).armed);
}

TEST_CASE("edge: the timings of a player's two sessions: a push arms only after the dwell") {
    // The seconds from the touch to the push at each arm in a player's report (edge, 0.5 s window): a thumb
    // brushing the rest while walking, or resting it and then pushing. Their median is 0.10 s.
    constexpr std::array<float, 25> kLogged = {0.07f, 0.29f, 0.03f, 0.01f, 0.05f, 0.46f, 0.29f, 0.24f, 0.03f,
                                               0.03f, 0.15f, 0.01f, 0.40f, 0.13f, 0.10f, 0.10f, 0.06f, 0.04f,
                                               0.22f, 0.14f, 0.06f, 0.11f, 0.01f, 0.19f, 0.40f};
    int armed = 0;
    int expected = 0;
    for (const float gap : kLogged) {
        CAPTURE(gap);
        const bool settled = gap >= kRestDwellSeconds;
        expected += settled ? 1 : 0;
        const Run run = restThenPush(RestWheelMode::Edge, gap, kUp, 0.3f);
        CHECK(run.armed == settled);
        armed += run.armed ? 1 : 0;
    }
    CHECK(armed == expected);
    CHECK(armed == 5);
}

TEST_CASE("while touched: a brush longer than 60 ms, while the stick moves or turns, leaves it alone") {
    for (const float brush : {0.07f, 0.1f, 0.15f, 0.2f}) {
        CAPTURE(brush);
        RestWheel w = wheel(RestWheelMode::Full);
        // Walking (the stick out for a while), the thumb brushes the rest and goes: never taken, nothing
        // latched.
        feed(w, frame(false, kRight), frames(0.5f));
        const Run touching = feed(w, frame(true, kRight), frames(brush));
        CHECK(touching.taken == 0);
        const Run after = feed(w, frame(false, kRight), frames(0.4f));
        CHECK(after.taken == 0);
        CHECK_FALSE(after.armed);
    }
}

TEST_CASE("while touched: a thumb that settles takes a stick already out, which waits for the centre") {
    RestWheel w = wheel(RestWheelMode::Full);
    feed(w, frame(false, kRight), frames(0.5f));
    const Run settled = feed(w, frame(true, kRight), frames(kRestDwellSeconds + 0.1f));
    CHECK(settled.taken > 0);
    CHECK_FALSE(settled.armed);
    feed(w, frame(true, kCentre), 3);
    CHECK(feed(w, frame(true, kUp), 2).armed);
}

TEST_CASE("while touched: a flick begun with the touch still picks at once, without the dwell") {
    // The deliberate quick pick the held lead protects (kHeldLeadSeconds): the stick leaves the centre just
    // before the touch registers, or with it. It is held from the game and picks on the landing frame.
    for (const float lead : {0.0f, 0.04f, 0.09f}) {
        CAPTURE(lead);
        RestWheel w = wheel(RestWheelMode::Full, RestWheelPick::Slots);
        feed(w, frame(false, kCentre), 10);
        feed(w, frame(false, kRight), frames(lead));
        const Run landing = feed(w, frame(true, kRight), 3);
        CHECK(landing.armed);
        CHECK(landing.taken == landing.count);
    }
    // A stick out for longer than the lead before the touch is a walk or a turn: not held.
    RestWheel late = wheel(RestWheelMode::Full, RestWheelPick::Slots);
    feed(late, frame(false, kCentre), 10);
    feed(late, frame(false, kRight), frames(0.3f));
    const Run brush = feed(late, frame(true, kRight), frames(0.15f));
    CHECK_FALSE(brush.armed);
    CHECK(brush.taken == 0);
}

TEST_CASE("edge: a forgiven gap does not restart the dwell and is not counted in it") {
    // A sensor that drops out 0.08 s while the thumb rests (inside the hold-over) is one touch.
    RestWheel w = wheel(RestWheelMode::Edge);
    feed(w, frame(true, kCentre), frames(0.1f));
    feed(w, frame(false, kCentre), frames(0.08f));
    feed(w, frame(true, kCentre), frames(0.2f));
    // 0.3 s of touch in all: settled. A restarted dwell (0.2 s) would not have been.
    CHECK(feed(w, frame(true, kUp), frames(0.2f)).armed);

    // 0.2 s of touch and a gap: not settled, however long the gap (it is not counted).
    RestWheel counted = wheel(RestWheelMode::Edge);
    feed(counted, frame(true, kCentre), frames(0.1f));
    feed(counted, frame(false, kCentre), frames(0.08f));
    feed(counted, frame(true, kCentre), frames(0.1f));
    CHECK_FALSE(feed(counted, frame(true, kUp), frames(0.3f)).armed);
    CHECK(counted.stats().bridgedGaps[0] == 1);
}

TEST_CASE("edge: a sensor that flickers while the thumb rests still arms; a real lift restarts the dwell") {
    RestWheel flicker = wheel(RestWheelMode::Edge);
    for (int cycle = 0; cycle < 4; ++cycle) {
        feed(flicker, frame(true, kCentre), frames(0.1f));
        feed(flicker, frame(false, kCentre), frames(0.08f));
    }
    feed(flicker, frame(true, kCentre), frames(0.1f));
    CHECK(feed(flicker, frame(true, kUp), frames(0.2f)).armed);

    // Settled, lifted past the hold-over, back for 0.1 s: a new landing with a new dwell.
    RestWheel lifted = wheel(RestWheelMode::Edge);
    feed(lifted, frame(true, kCentre), frames(kRestDwellSeconds + 0.05f));
    feed(lifted, frame(false, kCentre), frames(0.4f));
    feed(lifted, frame(true, kCentre), frames(0.1f));
    CHECK_FALSE(feed(lifted, frame(true, kUp), frames(0.2f)).armed);
    RestWheel again = wheel(RestWheelMode::Edge);
    feed(again, frame(true, kCentre), frames(kRestDwellSeconds + 0.05f));
    feed(again, frame(false, kCentre), frames(0.4f));
    feed(again, frame(true, kCentre), frames(kRestDwellSeconds + 0.05f));
    CHECK(feed(again, frame(true, kUp), frames(0.2f)).armed);
}

TEST_CASE("edge: only a sustained walk during the dwell uses the landing up, not a graze or a menu push") {
    // A graze shorter than the pick dwell: the deliberate push after the dwell arms.
    RestWheel graze = wheel(RestWheelMode::Edge);
    feed(graze, frame(true, kCentre), frames(0.05f));
    const Run touched = feed(graze, frame(true, kUp), frames(0.05f));
    CHECK(touched.voided == RestWheelVoid::None);
    feed(graze, frame(true, kCentre), frames(0.2f));
    CHECK(feed(graze, frame(true, kUp), frames(0.2f)).armed);

    // A sustained walk: used up, and the log says so.
    RestWheel walk = wheel(RestWheelMode::Edge);
    feed(walk, frame(true, kCentre), frames(0.05f));
    const Run walked = feed(walk, frame(true, kUp), frames(0.2f));
    CHECK(walked.voided == RestWheelVoid::Walk);
    feed(walk, frame(true, kCentre), frames(0.1f));
    CHECK_FALSE(feed(walk, frame(true, kUp), frames(0.2f)).armed);

    // A push while a menu holds input (blocked) is no walk.
    RestWheel menu = wheel(RestWheelMode::Edge);
    feed(menu, frame(true, kCentre), frames(0.05f));
    RestWheelFrame blocked = frame(true, kUp);
    blocked.blocked = true;
    const Run held = feed(menu, blocked, frames(0.2f));
    CHECK(held.voided == RestWheelVoid::None);
    feed(menu, frame(true, kCentre), frames(0.1f));
    CHECK(feed(menu, frame(true, kUp), frames(0.2f)).armed);
}

TEST_CASE("edge: the log's time since the touch is the real one, also for picks in a row") {
    RestWheel w = wheel(RestWheelMode::Edge);
    feed(w, frame(true, kCentre), frames(0.3f));
    const Run first = feed(w, frame(true, kUp), frames(0.4f));
    REQUIRE(first.armed);
    CHECK(first.sinceTouch == doctest::Approx(0.3f).epsilon(0.1));
    feed(w, frame(true, kCentre), frames(0.4f));
    const Run chained = feed(w, frame(true, kRight), 2);
    REQUIRE(chained.armed);
    // The stick has been back for 0.4 s, the window re-armed 0.05 s after it was centred: about 0.35 s since
    // the re-arm (counting a dwell in front of it too would read 0.6 s).
    CHECK(chained.sinceTouch > 0.25f);
    CHECK(chained.sinceTouch < 0.45f);
}

TEST_CASE("edge: the right rest and the left stick, and left-handed hands, wait for the dwell too") {
    // The right thumb rests, the left (move) stick is pushed.
    RestWheelFrame brush;
    brush.rest = {false, true};
    brush.sticks = {kUp, kCentre};
    RestWheelFrame rest = brush;
    rest.sticks = {kCentre, kCentre};
    RestWheel early = wheel(RestWheelMode::Edge);
    feed(early, rest, frames(0.1f));
    CHECK_FALSE(feed(early, brush, frames(0.5f)).armed);
    RestWheel settled = wheel(RestWheelMode::Edge);
    feed(settled, rest, frames(kRestDwellSeconds + 0.05f));
    CHECK(feed(settled, brush, frames(0.2f)).armed);

    // Left-handed hands (the move stick on the right): the left rest, the right stick.
    RestWheelHands lefty;
    lefty.hasRest = {true, true};
    lefty.moveStick = Hand::Right;
    lefty.turnStick = Hand::Left;
    RestWheelSettings settings;
    settings.mode = RestWheelMode::Edge;
    RestWheel lateLefty(settings, lefty);
    feed(lateLefty, frame(true, kCentre), frames(0.1f));
    CHECK_FALSE(feed(lateLefty, frame(true, kUp), frames(0.5f)).armed);
    RestWheel okLefty(settings, lefty);
    feed(okLefty, frame(true, kCentre), frames(kRestDwellSeconds + 0.05f));
    CHECK(feed(okLefty, frame(true, kUp), frames(0.2f)).armed);
}

TEST_CASE("edge: the other stick already out at the landing opens no window, dwell or not") {
    RestWheel w = wheel(RestWheelMode::Edge);
    feed(w, frame(false, kRight), frames(0.5f));
    const Run landing = feed(w, frame(true, kRight), frames(kRestDwellSeconds + 0.1f));
    CHECK(landing.voided == RestWheelVoid::StickOut);
    feed(w, frame(true, kCentre), frames(0.1f));
    CHECK_FALSE(feed(w, frame(true, kUp), frames(0.3f)).armed);
    // The thumb lifted and landed afresh with the stick centred: a window after the dwell.
    feed(w, frame(false, kCentre), frames(0.4f));
    feed(w, frame(true, kCentre), frames(kRestDwellSeconds + 0.05f));
    CHECK(feed(w, frame(true, kUp), frames(0.2f)).armed);
}
