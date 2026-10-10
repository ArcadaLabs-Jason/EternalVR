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

TEST_CASE("the touch counts: short touches, bridged gaps, quick returns and a stick already out") {
    RestWheel w = wheel(RestWheelMode::Full);
    // Three frames of touch: under the debounce, never registered.
    feed(w, frame(true, false, kCentre, kCentre), 3);
    feed(w, frame(false, false, kCentre, kCentre), 10);
    // A registered touch with a three-frame gap, then a lift and a return within the quick-return time.
    feed(w, frame(true, false, kCentre, kCentre), 10);
    feed(w, frame(false, false, kCentre, kCentre), 3);
    feed(w, frame(true, false, kCentre, kCentre), 10);
    feed(w, frame(false, false, kCentre, kCentre), 10);
    feed(w, frame(true, false, kCentre, kCentre), 10);
    const RestWheelStats& s = w.stats();
    CHECK(s.rawTouches[kLeft] == 4);
    CHECK(s.shortTouches[kLeft] == 1);
    CHECK(s.bridgedGaps[kLeft] == 1);
    CHECK(s.landings[kLeft] == 2);
    CHECK(s.quickReturns[kLeft] == 1);
    CHECK(s.stickOut[kLeft] == 0);
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
    // Three frames of touch (under the debounce) with the stick out: held meanwhile.
    for (int i = 0; i < 3; ++i) {
        CHECK(w.update(frame(true, false, kCentre, kRight), kFrame).taken[kRightHand]);
    }
    const RestWheelOutput back = w.update(frame(false, false, kCentre, kRight), kFrame);
    CHECK_FALSE(back.taken[kRightHand]);
    CHECK(back.event == RestWheelEvent::None);
    CHECK(w.stats().heldPushes[kLeft] == 0);
}

TEST_CASE("while touched: a stick already turning before the touch is not held, and waits for the centre") {
    RestWheel w = wheel(RestWheelMode::Full, RestWheelPick::Slots);
    feed(w, frame(false, false, kCentre, kRight), frames(0.3f));
    // The touch is sensed while the stick has been out for 0.3 s: it keeps turning until the touch registers.
    CHECK_FALSE(w.update(frame(true, false, kCentre, kRight), kFrame).taken[kRightHand]);
    bool armed = false;
    for (int i = 0; i < frames(0.2f); ++i) {
        armed = armed || w.update(frame(true, false, kCentre, kRight), kFrame).event == RestWheelEvent::Armed;
    }
    CHECK_FALSE(armed);
    CHECK(w.stats().heldPushes[kLeft] == 0);
    feed(w, frame(true, false, kCentre, kCentre), 2);
    CHECK(w.update(frame(true, false, kCentre, kRight), kFrame).event == RestWheelEvent::Armed);
}
