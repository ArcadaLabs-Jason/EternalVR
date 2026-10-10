// The thumb-rest wheel during a rest's hold-over (DECISIONS T-120): a push waits for the touch to decide, a
// pick under way needs the rest no longer, and under extreme a brushed rest leaves a pick alone.

#include "features/input/rest_wheel.hpp"

#include <doctest/doctest.h>

#include <cmath>
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
constexpr Axis2 kHalfRight{0.6f, 0.0f};
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

Run feed(RestWheel& w, const RestWheelFrame& f, int count) {
    Run run;
    for (int i = 0; i < count; ++i) {
        const RestWheelOutput out = w.update(f, kFrame);
        if (out.event != RestWheelEvent::None) {
            run.events.push_back(out.event);
        }
        if (out.slot) {
            run.slots.push_back(*out.slot);
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

TEST_CASE("edge: a push during the hold-over waits, then picks if the touch comes back") {
    RestWheel w = wheel(RestWheelMode::Edge);
    feed(w, frame(true, false, kCentre, kCentre), 8);
    feed(w, frame(false, false, kCentre, kCentre), 3);
    // Pushed while the touch is lost: held from the game, as centred to its gestures, and nothing armed.
    const Run push = feed(w, frame(false, false, kCentre, kUp), 2);
    CHECK(push.events.empty());
    CHECK(push.takenFrames[kRightHand] == 2);
    CHECK(push.deferredFrames[kRightHand] == 2);
    // The touch back, the stick still out: picking starts on that frame.
    const RestWheelOutput back = w.update(frame(true, false, kCentre, kUp), kFrame);
    CHECK(back.event == RestWheelEvent::Armed);
    CHECK(back.taken[kRightHand]);
    CHECK_FALSE(back.deferred[kRightHand]);
    CHECK(w.stats().heldPushes[kLeft] == 1);
}

TEST_CASE("edge: a push during the hold-over is the game's at once when the touch lifts") {
    RestWheel w = wheel(RestWheelMode::Edge);
    feed(w, frame(true, false, kCentre, kCentre), 8);
    feed(w, frame(false, false, kCentre, kCentre), 3);
    const Run run = feed(w, frame(false, false, kCentre, kUp), frames(0.2f));
    CHECK(run.events.empty());
    CHECK(run.takenFrames[kRightHand] > 0);
    // Given back the moment the hold-over lapsed, with the stick still out: not kept for the centre.
    CHECK_FALSE(run.last.taken[kRightHand]);
    CHECK_FALSE(run.last.deferred[kRightHand]);
    CHECK(run.takenFrames[kRightHand] < run.count);
    CHECK(w.stats().heldPushes[kLeft] == 0);
}

TEST_CASE(
    "while touched: a push during the hold-over waits, picks if the touch comes back, else is the game's") {
    RestWheel back = wheel(RestWheelMode::Full);
    feed(back, frame(true, false, kCentre, kCentre), frames(0.3f));
    feed(back, frame(false, false, kCentre, kCentre), 3);
    const Run push = feed(back, frame(false, false, kCentre, kUp), 3);
    CHECK(push.events.empty());
    CHECK(push.deferredFrames[kRightHand] == 3);
    CHECK(feed(back, frame(true, false, kCentre, kUp), 2).saw(RestWheelEvent::Armed));
    CHECK(back.stats().heldPushes[kLeft] == 1);

    RestWheel lifts = wheel(RestWheelMode::Full);
    feed(lifts, frame(true, false, kCentre, kCentre), frames(0.3f));
    feed(lifts, frame(false, false, kCentre, kCentre), 3);
    const Run run = feed(lifts, frame(false, false, kCentre, kUp), frames(0.2f));
    CHECK(run.events.empty());
    CHECK_FALSE(run.last.taken[kRightHand]);
    // The stick stays the game's while it is out (it does not wait for the centre).
    CHECK_FALSE(feed(lifts, frame(false, false, kCentre, kUp), 5).last.taken[kRightHand]);
}

TEST_CASE("extreme: a turn-stick push during the other rest's hold-over turns if the touch comes back, picks "
          "if it lapses") {
    // The left (restore) thumb rests: the turn stick is the game's.
    RestWheel back = wheel(RestWheelMode::Extreme);
    feed(back, frame(true, false, kCentre, kCentre), frames(0.3f));
    feed(back, frame(false, false, kCentre, kCentre), 3);
    const Run push = feed(back, frame(false, false, kCentre, kRight), 2);
    CHECK(push.deferredFrames[kRightHand] == 2);
    const Run turns = feed(back, frame(true, false, kCentre, kRight), 5);
    CHECK(turns.takenFrames[kRightHand] == 0);
    CHECK(turns.events.empty());

    RestWheel lapses = wheel(RestWheelMode::Extreme);
    feed(lapses, frame(true, false, kCentre, kCentre), frames(0.3f));
    feed(lapses, frame(false, false, kCentre, kCentre), 3);
    const Run picks = feed(lapses, frame(false, false, kCentre, kRight), frames(0.25f));
    CHECK(picks.saw(RestWheelEvent::Armed));
    CHECK(lapses.stats().heldPushes[kLeft] == 1);
}

TEST_CASE(
    "extreme: a push of the other stick during the turn hand's hold-over picks if the touch comes back, "
    "walks if it lapses") {
    // The right (turn hand's) thumb rests: the left stick picks.
    RestWheel back = wheel(RestWheelMode::Extreme, RestWheelPick::Slots);
    feed(back, frame(false, true, kCentre, kCentre), frames(0.3f));
    feed(back, frame(false, false, kCentre, kCentre), 3);
    const Run push = feed(back, frame(false, false, kUp, kCentre), 2);
    CHECK(push.events.empty());
    CHECK(push.deferredFrames[kLeft] == 2);
    CHECK(feed(back, frame(false, true, kUp, kCentre), 2).saw(RestWheelEvent::Armed));
    CHECK(back.stats().heldPushes[kRightHand] == 1);

    RestWheel lapses = wheel(RestWheelMode::Extreme, RestWheelPick::Slots);
    feed(lapses, frame(false, true, kCentre, kCentre), frames(0.3f));
    feed(lapses, frame(false, false, kCentre, kCentre), 3);
    const Run walks = feed(lapses, frame(false, false, kUp, kCentre), frames(0.25f));
    CHECK_FALSE(walks.saw(RestWheelEvent::Armed));
    CHECK_FALSE(walks.last.taken[kLeft]);
}

TEST_CASE("extreme: a brush on the other rest leaves a pick alone; a touch that stays ends it") {
    // The turn stick holds the game's wheel open; the left rest touched for one frame.
    RestWheel brush = wheel(RestWheelMode::Extreme);
    feed(brush, frame(false, false, kCentre, kCentre), 2);
    REQUIRE(feed(brush, frame(false, false, kCentre, kUp), frames(0.4f)).last.wheelDown);
    feed(brush, frame(true, false, kCentre, kUp), 1);
    const Run after = feed(brush, frame(false, false, kCentre, kUp), frames(0.2f));
    CHECK(after.wheelFrames == after.count);
    CHECK_FALSE(after.saw(RestWheelEvent::Released));

    // Picking by direction, a direction committed: a one-frame touch neither cancels nor picks.
    RestWheel slots = wheel(RestWheelMode::Extreme, RestWheelPick::Slots);
    feed(slots, frame(false, false, kCentre, kCentre), 2);
    feed(slots, frame(false, false, kCentre, kRight), 3);
    Run touched = feed(slots, frame(true, false, kCentre, kRight), 1);
    touched = feed(slots, frame(false, false, kCentre, kRight), frames(0.2f));
    CHECK(touched.events.empty());
    CHECK(feed(slots, frame(false, false, kCentre, kCentre), 2).slots ==
          std::vector<GameAction>{GameAction::WeaponSlot2});
}

TEST_CASE("while touched: a pick under way carries on after the thumb lifts, and ends on the stick") {
    RestWheel w = wheel(RestWheelMode::Full, RestWheelPick::Slots);
    feed(w, frame(true, false, kCentre, kCentre), frames(0.3f));
    // Half way out (no flick): picking, the direction not yet committed.
    REQUIRE(feed(w, frame(true, false, kCentre, kHalfRight), 2).saw(RestWheelEvent::Armed));
    const Run lifted = feed(w, frame(false, false, kCentre, kHalfRight), frames(0.4f));
    CHECK(lifted.events.empty());
    CHECK(lifted.takenFrames[kRightHand] == lifted.count);
    const Run let = feed(w, frame(false, false, kCentre, kCentre), 2);
    CHECK(let.slots == std::vector<GameAction>{GameAction::WeaponSlot2});
}

TEST_CASE("while touched: a flick up to 0.1 s before the touch is held and picks") {
    RestWheel w = wheel(RestWheelMode::Full, RestWheelPick::Slots);
    feed(w, frame(false, false, kCentre, kCentre), 4);
    feed(w, frame(false, false, kCentre, kRight), frames(0.07f));
    const RestWheelOutput landing = w.update(frame(true, false, kCentre, kRight), kFrame);
    CHECK(landing.taken[kRightHand]);
    CHECK(w.stats().heldPushes[kLeft] == 1);
    CHECK(feed(w, frame(true, false, kCentre, kRight), 3).saw(RestWheelEvent::Armed));
}

TEST_CASE("while touched: a one-frame brush while turning leaves the turn alone") {
    RestWheel w = wheel(RestWheelMode::Full);
    feed(w, frame(false, false, kCentre, kRight), frames(0.3f));
    Run run = feed(w, frame(true, false, kCentre, kRight), 1);
    CHECK(run.takenFrames[kRightHand] == 0);
    run = feed(w, frame(false, false, kCentre, kRight), frames(0.2f));
    CHECK(run.takenFrames[kRightHand] == 0);
    CHECK(run.events.empty());
}

TEST_CASE("quick returns count from when the touch went, not from the end of the hold-over") {
    // Back 0.3 s after the touch went (0.15 s after the lift): not a quick return.
    RestWheel slow = wheel(RestWheelMode::Full);
    feed(slow, frame(true, false, kCentre, kCentre), frames(0.3f));
    feed(slow, frame(false, false, kCentre, kCentre), frames(0.3f));
    feed(slow, frame(true, false, kCentre, kCentre), 2);
    CHECK(slow.stats().landings[kLeft] == 2);
    CHECK(slow.stats().quickReturns[kLeft] == 0);

    // Back 0.2 s after: a quick return.
    RestWheel quick = wheel(RestWheelMode::Full);
    feed(quick, frame(true, false, kCentre, kCentre), frames(0.3f));
    feed(quick, frame(false, false, kCentre, kCentre), frames(0.2f));
    feed(quick, frame(true, false, kCentre, kCentre), 2);
    CHECK(quick.stats().landings[kLeft] == 2);
    CHECK(quick.stats().quickReturns[kLeft] == 1);
}
