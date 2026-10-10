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
