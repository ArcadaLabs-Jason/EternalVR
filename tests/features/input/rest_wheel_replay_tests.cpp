// The thumb-rest wheel around a hold-over (DECISIONS T-120): brushes timed by the sensor's own time, a held
// push that comes back to the centre played once, a snap that waits for the touch, a pick cancelled when the
// thumb went to its own controls, and a push begun on the frame a route changes.

#include "features/input/rest_wheel.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <vector>

using evr::game::GameAction;
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

RestWheelFrame frame(bool leftRest, bool rightRest, Axis2 left, Axis2 right) {
    RestWheelFrame f;
    f.rest = {leftRest, rightRest};
    f.sticks = {left, right};
    return f;
}

struct Run {
    std::vector<RestWheelEvent> events;
    std::vector<GameAction> slots;
    std::vector<Axis2> replays; // the right stick's
    int takenFrames[2] = {0, 0};
    int deferredFrames[2] = {0, 0};
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

// `mixed`: frames of 1/72 s and 1/120 s in turn instead of 1/90 s.
Run feed(RestWheel& w, const RestWheelFrame& f, int count, bool mixed = false) {
    Run run;
    for (int i = 0; i < count; ++i) {
        const float dt = mixed ? (i % 2 == 0 ? 1.0f / 72.0f : 1.0f / 120.0f) : kFrame;
        const RestWheelOutput out = w.update(f, dt);
        if (out.event != RestWheelEvent::None) {
            run.events.push_back(out.event);
        }
        if (out.slot) {
            run.slots.push_back(*out.slot);
        }
        if (out.replay[kRightHand]) {
            run.replays.push_back(*out.replay[kRightHand]);
        }
        for (int h = 0; h < 2; ++h) {
            run.takenFrames[h] += out.taken[h] ? 1 : 0;
            run.deferredFrames[h] += out.deferred[h] ? 1 : 0;
        }
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

TEST_CASE("while touched: a two-frame brush while turning leaves the turn alone, at any frame rate") {
    for (const bool mixed : {false, true}) {
        CAPTURE(mixed);
        RestWheel w = wheel(RestWheelMode::Full);
        feed(w, frame(false, false, kCentre, kRight), frames(0.3f), mixed);
        Run run = feed(w, frame(true, false, kCentre, kRight), 2, mixed);
        CHECK(run.takenFrames[kRightHand] == 0);
        // The hold-over runs out (the floor) with the stick still turning: never taken, nothing latched.
        run = feed(w, frame(false, false, kCentre, kRight), frames(0.2f), mixed);
        CHECK(run.takenFrames[kRightHand] == 0);
        CHECK(run.events.empty());
    }
}

TEST_CASE("extreme: a two-frame brush on the other rest does not close an open wheel, at any frame rate") {
    for (const bool mixed : {false, true}) {
        CAPTURE(mixed);
        RestWheel w = wheel(RestWheelMode::Extreme);
        feed(w, frame(false, false, kCentre, kCentre), 2, mixed);
        REQUIRE(feed(w, frame(false, false, kCentre, kUp), frames(0.4f), mixed).last.wheelDown);
        feed(w, frame(true, false, kCentre, kUp), 2, mixed);
        const Run after = feed(w, frame(false, false, kCentre, kUp), frames(0.2f), mixed);
        CHECK(after.wheelFrames == after.count);
        CHECK_FALSE(after.saw(RestWheelEvent::Released));

        // Picking by direction, committed: a two-frame touch neither cancels nor picks.
        RestWheel slots = wheel(RestWheelMode::Extreme, RestWheelPick::Slots);
        feed(slots, frame(false, false, kCentre, kCentre), 2, mixed);
        feed(slots, frame(false, false, kCentre, kRight), 3, mixed);
        Run touched = feed(slots, frame(true, false, kCentre, kRight), 2, mixed);
        CHECK(touched.events.empty());
        touched = feed(slots, frame(false, false, kCentre, kRight), frames(0.2f), mixed);
        CHECK(touched.events.empty());
        CHECK(feed(slots, frame(false, false, kCentre, kCentre), 2, mixed).slots ==
              std::vector<GameAction>{GameAction::WeaponSlot2});
    }
}

TEST_CASE("a held push back in the centre before the hold-over lapses is played once to the game") {
    // Full: a flick of the turn stick up and back while the touch is lost; the hold-over then lapses.
    RestWheel w = wheel(RestWheelMode::Full);
    feed(w, frame(true, false, kCentre, kCentre), frames(0.3f));
    feed(w, frame(false, false, kCentre, kCentre), 3);
    Run run = feed(w, frame(false, false, kCentre, kUp), 2);
    CHECK(run.last.deferred[kRightHand]);
    run = feed(w, frame(false, false, kCentre, kCentre), frames(0.2f));
    REQUIRE(run.replays.size() == 1);
    CHECK(run.replays[0] == kUp);
    CHECK(run.events.empty());
    CHECK_FALSE(run.last.taken[kRightHand]);
    // Played once only.
    CHECK(feed(w, frame(false, false, kCentre, kCentre), 10).replays.empty());
}

TEST_CASE("weapon by direction: a flick held in a gap and back before the touch returns still picks") {
    RestWheel full = wheel(RestWheelMode::Full, RestWheelPick::Slots);
    feed(full, frame(true, false, kCentre, kCentre), frames(0.3f));
    feed(full, frame(false, false, kCentre, kCentre), 3);
    feed(full, frame(false, false, kCentre, kUp), 2);
    feed(full, frame(false, false, kCentre, kCentre), 2);
    Run run = feed(full, frame(true, false, kCentre, kCentre), 3);
    CHECK(run.saw(RestWheelEvent::Armed));
    CHECK(run.slots == std::vector<GameAction>{GameAction::WeaponSlot1});
    CHECK(full.stats().heldPushes[kLeft] == 1);

    // Under edge too, inside the window.
    RestWheel edge = wheel(RestWheelMode::Edge, RestWheelPick::Slots);
    feed(edge, frame(true, false, kCentre, kCentre), 8);
    feed(edge, frame(false, false, kCentre, kCentre), 2);
    feed(edge, frame(false, false, kCentre, kUp), 2);
    feed(edge, frame(false, false, kCentre, kCentre), 1);
    run = feed(edge, frame(true, false, kCentre, kCentre), 3);
    CHECK(run.slots == std::vector<GameAction>{GameAction::WeaponSlot1});
}

TEST_CASE("weapon by direction: a held flick caught on its way back when the touch returns still commits") {
    RestWheel w = wheel(RestWheelMode::Full, RestWheelPick::Slots);
    feed(w, frame(true, false, kCentre, kCentre), frames(0.3f));
    feed(w, frame(false, false, kCentre, kCentre), 3);
    feed(w, frame(false, false, kCentre, kRight), 4);
    // The touch back with the stick at 0.4, short of a direction: the flick is played from its furthest
    // point.
    const Axis2 back{0.4f, 0.0f};
    Run run = feed(w, frame(true, false, kCentre, back), 2);
    CHECK(run.saw(RestWheelEvent::Armed));
    run = feed(w, frame(true, false, kCentre, kCentre), 2);
    CHECK(run.slots == std::vector<GameAction>{GameAction::WeaponSlot2});
}

TEST_CASE("a held turn stick past a snap waits for the touch: the game's once the hold-over lapses") {
    for (const RestWheelMode mode : {RestWheelMode::Edge, RestWheelMode::Full}) {
        CAPTURE(static_cast<int>(mode));
        RestWheel w = wheel(mode);
        feed(w, frame(true, false, kCentre, kCentre), frames(0.3f));
        feed(w, frame(false, false, kCentre, kCentre), 3);
        // Held through the hold-over (0.15 s from the loss), then the game's, still out.
        const Run held = feed(w, frame(false, false, kCentre, kRight), 8);
        CHECK(held.deferredFrames[kRightHand] == 8);
        const Run lapsed = feed(w, frame(false, false, kCentre, kRight), 10);
        CHECK_FALSE(lapsed.last.taken[kRightHand]);
        CHECK_FALSE(lapsed.last.deferred[kRightHand]);
        CHECK(lapsed.events.empty());
        CHECK(w.stats().landings[kLeft] == 1);
    }
    // The touch back within the hold-over: it picks.
    RestWheel back = wheel(RestWheelMode::Full);
    feed(back, frame(true, false, kCentre, kCentre), frames(0.3f));
    feed(back, frame(false, false, kCentre, kCentre), 3);
    feed(back, frame(false, false, kCentre, kRight), 3);
    CHECK(feed(back, frame(true, false, kCentre, kRight), 2).saw(RestWheelEvent::Armed));
}

TEST_CASE(
    "weapon by direction while touched: a flick begun in a two-frame dropout picks, and does not turn") {
    RestWheel w = wheel(RestWheelMode::Full, RestWheelPick::Slots);
    feed(w, frame(true, false, kCentre, kCentre), frames(0.3f));
    // The rest off for two frames; the full flick starts on the first of them.
    Run run = feed(w, frame(false, false, kCentre, kRight), 2);
    CHECK(run.takenFrames[kRightHand] == 2);
    run = feed(w, frame(true, false, kCentre, kRight), 2);
    CHECK(run.saw(RestWheelEvent::Armed));
    CHECK(run.takenFrames[kRightHand] == 2);
    run = feed(w, frame(true, false, kCentre, kCentre), 2);
    CHECK(run.slots == std::vector<GameAction>{GameAction::WeaponSlot2});
    CHECK(w.stats().heldPushes[kLeft] == 1);
}

TEST_CASE("a lift on the thumb's own controls cancels a pick that has only just started") {
    // Full: the turn stick pushed, then the left thumb leaves its rest for its own stick: a turn, not a pick.
    RestWheel w = wheel(RestWheelMode::Full);
    feed(w, frame(true, false, kCentre, kCentre), frames(0.3f));
    REQUIRE(feed(w, frame(true, false, kCentre, kRight), 2).saw(RestWheelEvent::Armed));
    const RestWheelOutput cut = w.update(frame(false, false, kUp, kRight), kFrame);
    CHECK(cut.event == RestWheelEvent::Cancelled);
    CHECK(cut.deferred[kRightHand]); // centred to the gestures this frame, so they claim it anew
    const Run after = feed(w, frame(false, false, kUp, kRight), frames(0.3f));
    CHECK(after.wheelFrames == 0);
    CHECK(after.takenFrames[kRightHand] == 0);

    // A face button pressed (a jump) under edge does the same.
    RestWheel edge = wheel(RestWheelMode::Edge);
    feed(edge, frame(true, false, kCentre, kCentre), 8);
    REQUIRE(feed(edge, frame(true, false, kCentre, kRight), 1).saw(RestWheelEvent::Armed));
    RestWheelFrame jump = frame(false, false, kCentre, kRight);
    jump.faceButtons = {true, false};
    CHECK(edge.update(jump, kFrame).event == RestWheelEvent::Cancelled);
    CHECK(feed(edge, frame(false, false, kCentre, kRight), frames(0.3f)).wheelFrames == 0);

    // A touch sensor's cut does not: the pick carries on and the wheel opens.
    RestWheel touch = wheel(RestWheelMode::Full);
    feed(touch, frame(true, false, kCentre, kCentre), frames(0.3f));
    feed(touch, frame(true, false, kCentre, kRight), 2);
    RestWheelFrame sensor = frame(true, false, kCentre, kRight);
    sensor.thumbElsewhere = {true, false};
    feed(touch, sensor, 2);
    sensor.rest = {false, false};
    const Run carries = feed(touch, sensor, frames(0.3f));
    CHECK(touch.stats().cutLifts[kLeft] == 1);
    CHECK_FALSE(carries.saw(RestWheelEvent::Cancelled));
    CHECK(carries.wheelFrames > 0);

    // Nor does it cancel a direction already committed by a flick (weapon by direction).
    RestWheel slots = wheel(RestWheelMode::Full, RestWheelPick::Slots);
    feed(slots, frame(true, false, kCentre, kCentre), frames(0.3f));
    feed(slots, frame(true, false, kCentre, kRight), 2);
    feed(slots, frame(false, false, kUp, kRight), 2);
    CHECK(feed(slots, frame(false, false, kUp, kCentre), 2).slots ==
          std::vector<GameAction>{GameAction::WeaponSlot2});
}

TEST_CASE("extreme: the thumb from the turn hand's rest straight to its own stick picks with that push") {
    // The right thumb rests (the left stick picks); then it leaves for its own stick in one frame. No stick
    // touch sensor: its stick out of the centre is the evidence.
    RestWheel w = wheel(RestWheelMode::Extreme);
    feed(w, frame(false, true, kCentre, kCentre), frames(0.3f));
    const Run run = feed(w, frame(false, false, kCentre, kRight), 3);
    CHECK(w.stats().cutLifts[kRightHand] == 1);
    CHECK(run.saw(RestWheelEvent::Armed));
    CHECK(run.takenFrames[kRightHand] == 3);
}
