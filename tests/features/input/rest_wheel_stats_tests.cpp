// The thumb-rest wheel's touch counts for the log (RestWheelStats) and the pick's reach.

#include "features/input/rest_wheel.hpp"

#include <doctest/doctest.h>

#include <cmath>

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
using evr::input::RestWheelStats;

namespace {

constexpr float kFrame = 1.0f / 90.0f;
constexpr Axis2 kCentre{};
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

RestWheelFrame frame(bool leftRest, bool rightRest, Axis2 left, Axis2 right) {
    RestWheelFrame f;
    f.rest = {leftRest, rightRest};
    f.sticks = {left, right};
    return f;
}

void feed(RestWheel& w, const RestWheelFrame& f, int count) {
    for (int i = 0; i < count; ++i) {
        w.update(f, kFrame);
    }
}

int frames(float seconds) {
    return static_cast<int>(std::ceil(seconds / kFrame));
}

} // namespace

TEST_CASE("the touch counts: short touches, forgiven gaps, quick returns and a stick already out") {
    RestWheel w = wheel(RestWheelMode::Full);
    // Three frames of touch: a landing at once, counted as shorter than the debounce (flutter), then lifted.
    feed(w, frame(true, false, kCentre, kCentre), 3);
    feed(w, frame(false, false, kCentre, kCentre), frames(0.5f));
    // A touch with a three-frame gap (forgiven), then a lift and a return within the quick-return time.
    feed(w, frame(true, false, kCentre, kCentre), 10);
    feed(w, frame(false, false, kCentre, kCentre), 3);
    feed(w, frame(true, false, kCentre, kCentre), 10);
    feed(w, frame(false, false, kCentre, kCentre), frames(0.2f));
    feed(w, frame(true, false, kCentre, kCentre), 10);
    const RestWheelStats& s = w.stats();
    CHECK(s.rawTouches[kLeft] == 4);
    CHECK(s.shortTouches[kLeft] == 1);
    CHECK(s.bridgedGaps[kLeft] == 1);
    CHECK(s.landings[kLeft] == 3);
    CHECK(s.quickReturns[kLeft] == 1);
    CHECK(s.stickOut[kLeft] == 0);
    CHECK(s.vetoedLandings[kLeft] == 0);
    CHECK(s.cutLifts[kLeft] == 0);
    CHECK(s.rawTouches[kRightHand] == 0);

    // Landings with the turn stick out: one flicked with the touch, one out for a second before.
    RestWheel late = wheel(RestWheelMode::Full);
    feed(late, frame(false, false, kCentre, kRight), 2);
    feed(late, frame(true, false, kCentre, kRight), 10);
    feed(late, frame(false, false, kCentre, kCentre), frames(1.0f));
    feed(late, frame(false, false, kCentre, kRight), frames(1.0f));
    feed(late, frame(true, false, kCentre, kRight), 10);
    CHECK(late.stats().landings[kLeft] == 2);
    CHECK(late.stats().stickOut[kLeft] == 2);
    CHECK(late.stats().stickOutRecent[kLeft] == 1);
}

TEST_CASE("weapon by direction: the pick reports how far the stick went out") {
    RestWheel w = wheel(RestWheelMode::Full, RestWheelPick::Slots);
    feed(w, frame(true, false, kCentre, kCentre), 8);
    feed(w, frame(true, false, kCentre, Axis2{0.0f, 0.9f}), 3);
    const RestWheelOutput out = w.update(frame(true, false, kCentre, kCentre), kFrame);
    REQUIRE(out.event == RestWheelEvent::Picked);
    CHECK(out.slot.has_value());
    CHECK(out.peak == doctest::Approx(0.9f));
}

TEST_CASE("while touched: a flick that starts with the touch is held, then picks instead of turning") {
    RestWheel w = wheel(RestWheelMode::Full, RestWheelPick::Slots);
    feed(w, frame(false, false, kCentre, kCentre), 4);
    // The thumb lands and the turn stick flicks right on the same frame: held from the game while the touch
    // registers, then picking.
    bool alwaysTaken = true;
    bool armed = false;
    for (int i = 0; i < frames(0.2f); ++i) {
        const RestWheelOutput out = w.update(frame(true, false, kCentre, kRight), kFrame);
        alwaysTaken = alwaysTaken && out.taken[kRightHand];
        armed = armed || out.event == RestWheelEvent::Armed;
    }
    CHECK(alwaysTaken);
    CHECK(armed);
    const RestWheelOutput out = w.update(frame(true, false, kCentre, kCentre), kFrame);
    CHECK(out.event == RestWheelEvent::Picked);
    CHECK(out.slot.has_value());
    CHECK(w.stats().heldPushes[kLeft] == 1);
}

TEST_CASE("while touched: a touch that never registers gives the held stick back to the game") {
    RestWheel w = wheel(RestWheelMode::Full, RestWheelPick::Slots);
    feed(w, frame(false, false, kCentre, kCentre), 4);
    // Three frames of touch begun with the left thumb elsewhere (under the debounce) with the stick out:
    // held meanwhile.
    RestWheelFrame vetoed = frame(true, false, kCentre, kRight);
    vetoed.thumbElsewhere = {true, false};
    for (int i = 0; i < 3; ++i) {
        CHECK(w.update(vetoed, kFrame).taken[kRightHand]);
    }
    // The touch gone: still held through what could be a gap in the contact, then the game's.
    RestWheelFrame gone = frame(false, false, kCentre, kRight);
    gone.thumbElsewhere = {true, false};
    CHECK(w.update(gone, kFrame).taken[kRightHand]);
    bool back = false;
    for (int i = 0; i < frames(0.2f); ++i) {
        const RestWheelOutput out = w.update(gone, kFrame);
        back = back || !out.taken[kRightHand];
        CHECK(out.event == RestWheelEvent::None);
    }
    CHECK(back);
    CHECK(w.stats().heldPushes[kLeft] == 0);
    CHECK(w.stats().landings[kLeft] == 0);
}

TEST_CASE("while touched: a frame without the touch while it registers does not drop the held stick") {
    // The thumb still on its stick (a touch sensor), the rest flickering, the right stick flicked with it.
    RestWheel w = wheel(RestWheelMode::Full, RestWheelPick::Slots);
    feed(w, frame(false, false, kCentre, kCentre), 4);
    bool alwaysTaken = true;
    for (int i = 0; i < 6; ++i) {
        RestWheelFrame f = frame(i % 3 != 2, false, kCentre, kRight);
        f.thumbElsewhere = {true, false};
        alwaysTaken = alwaysTaken && w.update(f, kFrame).taken[kRightHand];
    }
    CHECK(alwaysTaken);
    // The contact (gaps bridged) passes the debounce: it registers and the held flick picks.
    RestWheelFrame on = frame(true, false, kCentre, kRight);
    on.thumbElsewhere = {true, false};
    feed(w, on, 2);
    CHECK(w.stats().landings[kLeft] == 1);
    CHECK(w.stats().heldPushes[kLeft] == 1);
}

TEST_CASE("while touched: a touch held back by the thumb elsewhere registers when the thumb comes back") {
    RestWheel w = wheel(RestWheelMode::Full, RestWheelPick::Slots);
    feed(w, frame(false, false, kCentre, kCentre), 4);
    // The left thumb still on its stick when the rest's touch begins, the right stick flicked with it.
    RestWheelFrame vetoed = frame(true, false, kCentre, kRight);
    vetoed.thumbElsewhere = {true, false};
    CHECK(w.update(vetoed, kFrame).taken[kRightHand]);
    CHECK(w.update(vetoed, kFrame).taken[kRightHand]);
    CHECK(w.stats().landings[kLeft] == 0);
    // Off the stick: the touch registers on that frame (before the debounce) and the held push picks.
    CHECK(w.update(frame(true, false, kCentre, kRight), kFrame).taken[kRightHand]);
    CHECK(w.stats().landings[kLeft] == 1);
    CHECK(w.stats().vetoedLandings[kLeft] == 1);
    CHECK(w.stats().heldPushes[kLeft] == 1);
    bool armed = false;
    for (int i = 0; i < 5; ++i) {
        armed = armed || w.update(frame(true, false, kCentre, kRight), kFrame).event == RestWheelEvent::Armed;
    }
    CHECK(armed);
    const RestWheelOutput out = w.update(frame(true, false, kCentre, kCentre), kFrame);
    CHECK(out.event == RestWheelEvent::Picked);
}

TEST_CASE("while touched: a stick turning when the thumb lands is taken once the landing has lasted the "
          "debounce, and waits for the centre") {
    RestWheel w = wheel(RestWheelMode::Full, RestWheelPick::Slots);
    feed(w, frame(false, false, kCentre, kRight), frames(0.3f));
    // The touch registers on its first frame with the stick out for 0.3 s: a brush could be all it is, so the
    // stick keeps turning until the landing is kRestDebounceSeconds old, then picks nothing until it has been
    // back in the centre.
    CHECK_FALSE(w.update(frame(true, false, kCentre, kRight), kFrame).taken[kRightHand]);
    CHECK_FALSE(w.update(frame(true, false, kCentre, kRight), kFrame).taken[kRightHand]);
    feed(w, frame(true, false, kCentre, kRight), frames(0.06f));
    CHECK(w.update(frame(true, false, kCentre, kRight), kFrame).taken[kRightHand]);
    bool armed = false;
    for (int i = 0; i < frames(0.2f); ++i) {
        armed = armed || w.update(frame(true, false, kCentre, kRight), kFrame).event == RestWheelEvent::Armed;
    }
    CHECK_FALSE(armed);
    CHECK(w.stats().heldPushes[kLeft] == 0);
    feed(w, frame(true, false, kCentre, kCentre), 2);
    CHECK(w.update(frame(true, false, kCentre, kRight), kFrame).event == RestWheelEvent::Armed);
}
