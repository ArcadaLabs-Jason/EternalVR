// The thumb-rest wheel's touch (DECISIONS T-120): a touch registers on its first frame unless the thumb is
// elsewhere on that controller, a short loss is a forgiven gap, and a loss after the thumb went elsewhere
// lifts at once.

#include "features/input/rest_wheel.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using evr::input::Axis2;
using evr::input::Hand;
using evr::input::RestWheel;
using evr::input::RestWheelEvent;
using evr::input::RestWheelFrame;
using evr::input::RestWheelHands;
using evr::input::RestWheelMode;
using evr::input::RestWheelOutput;
using evr::input::RestWheelPick;
using evr::input::RestWheelSettings;

namespace {

constexpr float kFrame = 1.0f / 90.0f;
constexpr Axis2 kCentre{};
constexpr Axis2 kUp{0.0f, 1.0f};
constexpr Axis2 kRight{1.0f, 0.0f};
constexpr int kLeft = 0;
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

// The left rest touched or not, a touch sensor beside it (the left thumb elsewhere) or not, the right (turn)
// stick, and the left stick (the resting thumb's own).
RestWheelFrame frame(bool rest, bool elsewhere, Axis2 right = kCentre, Axis2 left = kCentre) {
    RestWheelFrame f;
    f.rest = {rest, false};
    f.thumbElsewhere = {elsewhere, false};
    f.sticks = {left, right};
    return f;
}

// ... with the left hand's face button pressed.
RestWheelFrame pressed(RestWheelFrame f) {
    f.faceButtons = {true, false};
    return f;
}

struct Run {
    std::vector<RestWheelEvent> events;
    int takenFrames = 0; // the right stick
    int wheelFrames = 0;
    int count = 0;
    RestWheelOutput last;

    [[nodiscard]] bool saw(RestWheelEvent event) const {
        for (const RestWheelEvent e : events) {
            if (e == event) {
                return true;
            }
        }
        return false;
    }
};

Run feed(RestWheel& w, const RestWheelFrame& f, int count) {
    Run run;
    for (int i = 0; i < count; ++i) {
        const RestWheelOutput out = w.update(f, kFrame);
        if (out.event != RestWheelEvent::None) {
            run.events.push_back(out.event);
        }
        run.takenFrames += out.taken[kRightHand] ? 1 : 0;
        run.wheelFrames += out.wheelDown ? 1 : 0;
        ++run.count;
        run.last = out;
    }
    return run;
}

int frames(float seconds) {
    return static_cast<int>(std::ceil(seconds / kFrame));
}

} // namespace

TEST_CASE("a touch registers on its first frame") {
    RestWheel edge = wheel(RestWheelMode::Edge);
    edge.update(frame(true, false), kFrame);
    CHECK(edge.stats().landings[kLeft] == 1);
    CHECK(edge.update(frame(true, false, kUp), kFrame).event == RestWheelEvent::Armed);

    // Under extreme the turn stick is given back on that frame (nothing was being picked).
    RestWheel extreme = wheel(RestWheelMode::Extreme);
    CHECK(feed(extreme, frame(false, false), 2).last.taken[kRightHand]);
    CHECK_FALSE(extreme.update(frame(true, false), kFrame).taken[kRightHand]);
}

TEST_CASE("a loss shorter than the hold-over neither lifts nor lands again") {
    // Under while touched the stick stays the wheel's through a 0.1 s loss.
    RestWheel full = wheel(RestWheelMode::Full);
    feed(full, frame(true, false), frames(0.3f));
    const Run gap = feed(full, frame(false, false), frames(0.1f));
    CHECK(gap.takenFrames == gap.count);
    feed(full, frame(true, false), 10);
    CHECK(full.stats().landings[kLeft] == 1);
    CHECK(full.stats().bridgedGaps[kLeft] == 1);
    CHECK(full.stats().rawTouches[kLeft] == 2);

    // An open wheel stays open through it.
    RestWheel open = wheel(RestWheelMode::Edge);
    feed(open, frame(true, false), 8);
    REQUIRE(feed(open, frame(true, false, kUp), frames(0.5f)).last.wheelDown);
    const Run held = feed(open, frame(false, false, kUp), frames(0.1f));
    CHECK(held.wheelFrames == held.count);
    CHECK_FALSE(held.saw(RestWheelEvent::Released));
    CHECK(feed(open, frame(true, false, kUp), 5).last.wheelDown);

    // No new landing from a gap shorter than the debounce: after the window, the touch back opens none.
    RestWheel edge = wheel(RestWheelMode::Edge);
    feed(edge, frame(true, false), frames(0.6f));
    feed(edge, frame(false, false), 3);
    feed(edge, frame(true, false), 2);
    CHECK(feed(edge, frame(true, false, kUp), 10).events.empty());
    CHECK(edge.stats().landings[kLeft] == 1);
}

TEST_CASE("a loss longer than the hold-over lifts, once the hold-over has passed") {
    RestWheel full = wheel(RestWheelMode::Full);
    feed(full, frame(true, false), frames(0.3f));
    CHECK(feed(full, frame(false, false), frames(0.12f)).last.taken[kRightHand]);
    CHECK_FALSE(feed(full, frame(false, false), frames(0.08f)).last.taken[kRightHand]);
    // Back on: a new landing.
    feed(full, frame(true, false), 2);
    CHECK(full.stats().landings[kLeft] == 2);
    CHECK(full.stats().bridgedGaps[kLeft] == 0);
    CHECK(full.stats().cutLifts[kLeft] == 0);
}

TEST_CASE("a brief touch has a short hold-over: never under its floor, never past how long it was on") {
    // One frame of touch: still resting three frames later (the floor), lifted once the floor has passed.
    RestWheel full = wheel(RestWheelMode::Full);
    full.update(frame(true, false), kFrame);
    CHECK(feed(full, frame(false, false), 3).last.taken[kRightHand]);
    CHECK_FALSE(feed(full, frame(false, false), frames(0.04f)).last.taken[kRightHand]);

    // It opens no window that lasts: a push 0.1 s later is the stick's own.
    RestWheel edge = wheel(RestWheelMode::Edge);
    edge.update(frame(true, false), kFrame);
    feed(edge, frame(false, false), frames(0.1f));
    CHECK(feed(edge, frame(false, false, kUp), 5).events.empty());
}

TEST_CASE("a touch begun with the thumb elsewhere waits until the thumb leaves, or for the debounce") {
    // The thumb leaves its stick or face buttons while still touching the rest: the landing is on that frame.
    RestWheel leaves = wheel(RestWheelMode::Edge);
    feed(leaves, frame(true, true), 3);
    CHECK(leaves.stats().landings[kLeft] == 0);
    CHECK(leaves.stats().vetoedLandings[kLeft] == 1);
    feed(leaves, frame(true, false), 1);
    CHECK(leaves.stats().landings[kLeft] == 1);
    CHECK(leaves.update(frame(true, false, kUp), kFrame).event == RestWheelEvent::Armed);

    // A sensor that sees the resting thumb as on the stick too: the touch registers after the debounce.
    RestWheel steady = wheel(RestWheelMode::Edge);
    feed(steady, frame(true, true), 5);
    CHECK(steady.stats().landings[kLeft] == 0);
    feed(steady, frame(true, true), frames(0.1f));
    CHECK(steady.stats().landings[kLeft] == 1);

    // Gone before either: no landing at all.
    RestWheel brief = wheel(RestWheelMode::Edge);
    feed(brief, frame(true, true), 3);
    feed(brief, frame(false, true), frames(0.3f));
    feed(brief, frame(false, false), 2);
    CHECK(brief.stats().landings[kLeft] == 0);
    CHECK(brief.stats().shortTouches[kLeft] == 1);
    CHECK(feed(brief, frame(false, false, kUp), 10).events.empty());
}

TEST_CASE("the debounce counts the contact: a rest flickering under a touch sensor still registers") {
    // On two frames, off one, with the stick's touch on all along: no single run of the sensor lasts the
    // debounce, the contact with its gaps bridged does.
    RestWheel w = wheel(RestWheelMode::Edge);
    for (int i = 0; i < 12; ++i) {
        w.update(frame(i % 3 != 2, true), kFrame);
    }
    CHECK(w.stats().landings[kLeft] == 1);
    CHECK(w.stats().vetoedLandings[kLeft] == 1);
}

TEST_CASE("a touch sensor on all along while the rest comes and goes no longer holds a landing back") {
    // The rest on for a frame, then off past the hold-over, again and again, the stick's touch on throughout.
    RestWheel w = wheel(RestWheelMode::Edge);
    const auto cycle = [&w](int count) {
        for (int c = 0; c < count; ++c) {
            w.update(frame(true, true), kFrame);
            feed(w, frame(false, true), 20);
        }
    };
    cycle(6); // 1.4 s
    CHECK(w.stats().landings[kLeft] == 0);
    cycle(2);
    CHECK(w.stats().landings[kLeft] >= 1);
    CHECK(w.stats().stuckVetoes[kLeft] >= 1);
}

TEST_CASE("its own stick out of the centre is the thumb on it") {
    // A touch of the rest with its own stick pushed waits for the stick to come back.
    RestWheel landing = wheel(RestWheelMode::Edge);
    feed(landing, frame(true, false, kCentre, kUp), 3);
    CHECK(landing.stats().landings[kLeft] == 0);
    CHECK(landing.stats().vetoedLandings[kLeft] == 1);
    feed(landing, frame(true, false), 1);
    CHECK(landing.stats().landings[kLeft] == 1);

    // Its own stick pushed during a loss lifts at once.
    RestWheel full = wheel(RestWheelMode::Full);
    feed(full, frame(true, false), frames(0.3f));
    CHECK(feed(full, frame(false, false), 3).last.taken[kRightHand]);
    CHECK_FALSE(full.update(frame(false, false, kCentre, kUp), kFrame).taken[kRightHand]);
    CHECK(full.stats().cutLifts[kLeft] == 1);
}

TEST_CASE("a touch sensor cuts a loss once steady; a press at once") {
    // The stick's touch during a loss: lifted only once it has been on kVetoTouchSteadySeconds.
    RestWheel touch = wheel(RestWheelMode::Full);
    feed(touch, frame(true, false), frames(0.3f));
    feed(touch, frame(false, false), 3);
    CHECK(touch.update(frame(false, true), kFrame).taken[kRightHand]);
    CHECK_FALSE(feed(touch, frame(false, true), 4).last.taken[kRightHand]);
    CHECK(touch.stats().cutLifts[kLeft] == 1);

    // A sensor that flickers in step with the rest (on while it is off) cuts nothing.
    RestWheel flicker = wheel(RestWheelMode::Full);
    feed(flicker, frame(true, false), frames(0.3f));
    int taken = 0;
    for (int i = 0; i < 40; ++i) {
        const bool rest = i % 4 < 2;
        taken += flicker.update(frame(rest, !rest), kFrame).taken[kRightHand] ? 1 : 0;
    }
    CHECK(taken == 40);
    CHECK(flicker.stats().cutLifts[kLeft] == 0);

    // A face button pressed during a loss: lifted on that frame.
    RestWheel press = wheel(RestWheelMode::Full);
    feed(press, frame(true, false), frames(0.3f));
    feed(press, frame(false, false), 3);
    CHECK_FALSE(press.update(pressed(frame(false, false)), kFrame).taken[kRightHand]);
    CHECK(press.stats().cutLifts[kLeft] == 1);

    // The sensor came on after the landing and is still on when the touch goes: lifted at once.
    RestWheel moved = wheel(RestWheelMode::Full);
    feed(moved, frame(true, false), frames(0.3f));
    CHECK(feed(moved, frame(true, true), 6).last.taken[kRightHand]);
    CHECK_FALSE(moved.update(frame(false, true), kFrame).taken[kRightHand]);
    CHECK(moved.stats().cutLifts[kLeft] == 1);
}

TEST_CASE("the thumb elsewhere since before the landing does not cut a loss short") {
    RestWheel w = wheel(RestWheelMode::Full);
    // Registered after the debounce with the stick's touch on all along, which stays on.
    feed(w, frame(true, true), frames(0.4f));
    REQUIRE(w.stats().landings[kLeft] == 1);
    const Run gap = feed(w, frame(false, true), frames(0.1f));
    CHECK(gap.takenFrames == gap.count);
    feed(w, frame(true, true), 5);
    CHECK(w.stats().landings[kLeft] == 1);
    CHECK(w.stats().cutLifts[kLeft] == 0);
    CHECK(w.stats().bridgedGaps[kLeft] == 1);
    // It lifts after the hold-over as any loss does.
    CHECK_FALSE(feed(w, frame(false, true), frames(0.2f)).last.taken[kRightHand]);
    CHECK(w.stats().cutLifts[kLeft] == 0);
}

TEST_CASE("the thumb going elsewhere while the touch is still sensed changes nothing") {
    RestWheel full = wheel(RestWheelMode::Full);
    feed(full, frame(true, false), 10);
    const Run veto = feed(full, frame(true, true), frames(0.3f));
    CHECK(veto.takenFrames == veto.count);
    feed(full, frame(true, false), frames(0.3f));
    CHECK(full.stats().landings[kLeft] == 1);
    CHECK(full.stats().cutLifts[kLeft] == 0);

    RestWheel open = wheel(RestWheelMode::Edge);
    feed(open, frame(true, false), 8);
    feed(open, frame(true, false, kUp), frames(0.5f));
    const Run held = feed(open, frame(true, true, kUp), frames(0.3f));
    CHECK(held.wheelFrames == held.count);
    CHECK(held.events.empty());
}

TEST_CASE("edge: a gap past the debounce is a fresh landing, with the other stick centred and no cancel") {
    // After the window, a 0.1 s gap (forgiven, no lift) and the touch back: the window opens again.
    RestWheel w = wheel(RestWheelMode::Edge);
    feed(w, frame(true, false), frames(0.6f));
    feed(w, frame(false, false), frames(0.1f));
    feed(w, frame(true, false), 2);
    CHECK(w.stats().landings[kLeft] == 2);
    CHECK(w.update(frame(true, false, kUp), kFrame).event == RestWheelEvent::Armed);

    // Not after a cancel since the landing.
    RestWheel cancelled = wheel(RestWheelMode::Edge);
    feed(cancelled, frame(true, false), 8);
    Run run = feed(cancelled, frame(true, false, kUp), 5);
    run = feed(cancelled, frame(true, false), frames(0.6f));
    REQUIRE(run.saw(RestWheelEvent::Cancelled));
    feed(cancelled, frame(false, false), frames(0.1f));
    feed(cancelled, frame(true, false), 2);
    CHECK(cancelled.stats().landings[kLeft] == 1);
    CHECK(feed(cancelled, frame(true, false, kUp), 10).events.empty());

    // Nor with the other stick out of the centre less than kOwnStickVoidSeconds before (turning, then the
    // thumb's gap).
    RestWheel turning = wheel(RestWheelMode::Edge);
    feed(turning, frame(true, false), frames(0.6f));
    feed(turning, frame(true, false, kRight), frames(0.2f));
    feed(turning, frame(true, false), frames(0.1f));
    feed(turning, frame(false, false), frames(0.1f));
    feed(turning, frame(true, false), 2);
    CHECK(turning.stats().landings[kLeft] == 1);
    CHECK(feed(turning, frame(true, false, kUp), 10).events.empty());
}
