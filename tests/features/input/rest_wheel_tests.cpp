#include "features/input/rest_wheel.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <optional>
#include <ostream>
#include <vector>

using evr::game::GameAction;
using evr::input::Axis2;
using evr::input::Hand;
using evr::input::kMinWheelHoldSeconds;
using evr::input::RestWheel;
using evr::input::RestWheelEvent;
using evr::input::RestWheelFrame;
using evr::input::RestWheelHands;
using evr::input::RestWheelMode;
using evr::input::RestWheelOutput;
using evr::input::RestWheelPick;
using evr::input::RestWheelSettings;
using evr::input::RestWheelVoid;
using evr::input::WheelDirection;
using evr::input::WheelTick;

namespace {

constexpr float kFrame = 1.0f / 90.0f;
constexpr Axis2 kCentre{};
constexpr Axis2 kUp{0.0f, 1.0f};
constexpr Axis2 kRight{1.0f, 0.0f};
constexpr int kLeft = 0;
constexpr int kRightHand = 1;

// Right-handed: the move stick on the left, the turn stick on the right, a thumb rest on both.
RestWheelHands touchHands() {
    RestWheelHands hands;
    hands.hasRest = {true, true};
    hands.moveStick = Hand::Left;
    hands.turnStick = Hand::Right;
    return hands;
}

RestWheel wheel(RestWheelMode mode = RestWheelMode::Edge, RestWheelPick pick = RestWheelPick::Wheel) {
    RestWheelSettings settings;
    settings.mode = mode;
    settings.pick = pick;
    return RestWheel(settings, touchHands());
}

RestWheelFrame frame(bool leftRest, bool rightRest, Axis2 left, Axis2 right, bool blocked = false) {
    RestWheelFrame f;
    f.rest = {leftRest, rightRest};
    f.sticks = {left, right};
    f.blocked = blocked;
    return f;
}

// What a run of frames did.
struct Run {
    std::vector<RestWheelEvent> events;
    int wheelFrames = 0;
    int takenFrames[2] = {0, 0};
    int armTicks = 0;
    int pickTicks = 0;
    std::vector<GameAction> slots;
    RestWheelVoid voided = RestWheelVoid::None; // why the last voided landing opened no window
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

Run feed(RestWheel& w, const RestWheelFrame& f, int frames, Run run = {}) {
    for (int i = 0; i < frames; ++i) {
        const RestWheelOutput out = w.update(f, kFrame);
        if (out.event != RestWheelEvent::None) {
            run.events.push_back(out.event);
        }
        if (out.event == RestWheelEvent::Voided) {
            run.voided = out.voided;
        }
        run.wheelFrames += out.wheelDown ? 1 : 0;
        for (int h = 0; h < 2; ++h) {
            run.takenFrames[h] += out.taken[h] ? 1 : 0;
            run.armTicks += out.tick[h] == WheelTick::Arm ? 1 : 0;
            run.pickTicks += out.tick[h] == WheelTick::Pick ? 1 : 0;
        }
        if (out.slot) {
            run.slots.push_back(*out.slot);
        }
        run.last = out;
    }
    return run;
}

int frames(float seconds) {
    return static_cast<int>(std::ceil(seconds / kFrame));
}

// The left thumb rests (past the debounce), both sticks centred.
void landLeft(RestWheel& w) {
    feed(w, frame(true, false, kCentre, kCentre), 8);
}

} // namespace

TEST_CASE("touch, then push: the other stick opens the game's wheel and letting go picks") {
    RestWheel w = wheel();
    REQUIRE(w.usable());
    landLeft(w);
    // The turn stick pushed up within the window: picking starts on that frame, with a tick on its hand.
    RestWheelOutput first = w.update(frame(true, false, kCentre, kUp), kFrame);
    CHECK(first.event == RestWheelEvent::Armed);
    CHECK(first.restHand == Hand::Left);
    CHECK(first.stickHand == Hand::Right);
    CHECK(first.tick[kRightHand] == WheelTick::Arm);
    CHECK(first.taken[kRightHand]);
    CHECK_FALSE(first.taken[kLeft]);
    CHECK_FALSE(first.wheelDown);
    // Held up past the dwell: the wheel is held and points up.
    Run held = feed(w, frame(true, false, kCentre, kUp), frames(0.15f));
    CHECK(held.saw(RestWheelEvent::Opened));
    CHECK(held.last.wheelDown);
    CHECK(held.last.pointer == kUp);
    // Turned right: the pointer follows.
    held = feed(w, frame(true, false, kCentre, kRight), frames(0.3f));
    CHECK(held.last.pointer == kRight);
    // Let go: the wheel is released (the game picks) with a tick, and the stick is free again.
    Run after = feed(w, frame(true, false, kCentre, kCentre), 3);
    CHECK(after.saw(RestWheelEvent::Released));
    CHECK(after.pickTicks == 1);
    CHECK_FALSE(after.last.wheelDown);
    CHECK_FALSE(after.last.taken[kRightHand]);
}

TEST_CASE("the move stick picks when the right thumb rests") {
    RestWheel w = wheel();
    feed(w, frame(false, true, kCentre, kCentre), 8);
    const RestWheelOutput first = w.update(frame(false, true, kUp, kCentre), kFrame);
    CHECK(first.event == RestWheelEvent::Armed);
    CHECK(first.stickHand == Hand::Left);
    CHECK(first.taken[kLeft]);
    CHECK_FALSE(first.taken[kRightHand]);
}

TEST_CASE("after the window a resting thumb leaves the other stick alone") {
    RestWheel w = wheel();
    feed(w, frame(true, false, kCentre, kCentre), frames(0.06f) + frames(0.55f));
    const Run run = feed(w, frame(true, false, kCentre, kRight), frames(0.5f));
    CHECK(run.events.empty());
    CHECK(run.takenFrames[kRightHand] == 0);
    CHECK(run.wheelFrames == 0);
}

TEST_CASE("a push inside the window arms; the window counts from the debounced landing") {
    RestWheel w = wheel();
    feed(w, frame(true, false, kCentre, kCentre), frames(0.06f) + frames(0.4f));
    CHECK(w.update(frame(true, false, kCentre, kRight), kFrame).event == RestWheelEvent::Armed);
}

TEST_CASE("a thumb landing while a stick is out does nothing until it lands again") {
    RestWheel w = wheel();
    // The turn stick turns while the left thumb lands: void.
    const Run landing = feed(w, frame(true, false, kCentre, kRight), 10);
    CHECK(landing.saw(RestWheelEvent::Voided));
    CHECK(landing.takenFrames[kRightHand] == 0);
    feed(w, frame(true, false, kCentre, kCentre), 3);
    CHECK(feed(w, frame(true, false, kCentre, kUp), 20).events.empty());
    // The thumb's own stick out at the landing voids it as well.
    RestWheel own = wheel();
    feed(own, frame(true, false, kUp, kCentre), 10);
    feed(own, frame(true, false, kCentre, kCentre), 3);
    CHECK(feed(own, frame(true, false, kCentre, kUp), 20).events.empty());
    // Lifted and landed again with the sticks centred: it arms.
    feed(w, frame(false, false, kCentre, kCentre), 10);
    landLeft(w);
    CHECK(w.update(frame(true, false, kCentre, kUp), kFrame).event == RestWheelEvent::Armed);
}

TEST_CASE("a touch shorter than the debounce is no landing, and a brief lift does not end picking") {
    RestWheel w = wheel();
    feed(w, frame(true, false, kCentre, kCentre), 3);
    feed(w, frame(false, false, kCentre, kCentre), 10);
    CHECK(feed(w, frame(false, false, kCentre, kUp), 20).events.empty());

    RestWheel held = wheel();
    landLeft(held);
    feed(held, frame(true, false, kCentre, kUp), frames(0.2f));
    // Three frames of lost touch while the wheel is open: still open.
    const Run glitch = feed(held, frame(false, false, kCentre, kUp), 3);
    CHECK(glitch.last.wheelDown);
    CHECK_FALSE(glitch.saw(RestWheelEvent::Released));
}

TEST_CASE("a flick shorter than the dwell presses nothing") {
    RestWheel w = wheel();
    landLeft(w);
    Run run = feed(w, frame(true, false, kCentre, kUp), 5);
    run = feed(w, frame(true, false, kCentre, kCentre), 3, run);
    CHECK(run.saw(RestWheelEvent::Armed));
    CHECK(run.saw(RestWheelEvent::Cancelled));
    CHECK(run.wheelFrames == 0);
    CHECK(run.slots.empty());
    CHECK(run.pickTicks == 0);
    CHECK_FALSE(run.last.taken[kRightHand]);

    // Wandering between directions keeps restarting the dwell: nothing is pressed either.
    RestWheel wander = wheel();
    landLeft(wander);
    Run wandering;
    for (int i = 0; i < 6; ++i) {
        wandering = feed(wander, frame(true, false, kCentre, i % 2 == 0 ? kUp : kRight), 5, wandering);
    }
    CHECK(wandering.wheelFrames == 0);
}

TEST_CASE("a quick pick holds the wheel at least the minimum time") {
    RestWheel w = wheel();
    landLeft(w);
    Run run = feed(w, frame(true, false, kCentre, kUp), frames(0.12f));
    REQUIRE(run.saw(RestWheelEvent::Opened));
    const int heldBefore = run.wheelFrames;
    run = feed(w, frame(true, false, kCentre, kCentre), frames(0.5f), run);
    CHECK(run.saw(RestWheelEvent::Released));
    const float heldSeconds = static_cast<float>(run.wheelFrames) * kFrame;
    CHECK(heldSeconds >= kMinWheelHoldSeconds - kFrame);
    CHECK(run.wheelFrames > heldBefore);
    // The pointer stays on the last direction while the wheel waits to close.
    CHECK(run.pickTicks == 1);
}

TEST_CASE("the pointer keeps the last direction while the stick falls back toward the centre") {
    RestWheel w = wheel();
    landLeft(w);
    feed(w, frame(true, false, kCentre, kRight), frames(0.4f));
    const RestWheelOutput inside = w.update(frame(true, false, kCentre, Axis2{0.0f, 0.4f}), kFrame);
    CHECK(inside.wheelDown);
    CHECK(inside.pointer == kRight);
}

TEST_CASE("lifting the thumb closes the wheel") {
    RestWheel w = wheel();
    landLeft(w);
    feed(w, frame(true, false, kCentre, kUp), frames(0.4f));
    const Run lifted = feed(w, frame(false, false, kCentre, kUp), frames(0.2f));
    CHECK(lifted.saw(RestWheelEvent::Released));
    CHECK_FALSE(lifted.last.wheelDown);
    // The stick, still up, stays out of play until it comes back.
    CHECK(lifted.last.taken[kRightHand]);
    CHECK_FALSE(feed(w, frame(false, false, kCentre, kCentre), 1).last.taken[kRightHand]);
}

TEST_CASE("weapon by direction presses the slot of the direction held, on letting go") {
    RestWheel w = wheel(RestWheelMode::Edge, RestWheelPick::Slots);
    landLeft(w);
    Run run = feed(w, frame(true, false, kCentre, kUp), frames(0.3f));
    CHECK(run.wheelFrames == 0);
    CHECK(run.slots.empty());
    run = feed(w, frame(true, false, kCentre, kCentre), 3, run);
    CHECK(run.slots == std::vector<GameAction>{GameAction::WeaponSlot1});
    CHECK(run.saw(RestWheelEvent::Picked));
    CHECK(run.pickTicks == 1);
    CHECK(run.wheelFrames == 0);

    // The last direction held long enough counts; a flick on the way back does not.
    RestWheel moved = wheel(RestWheelMode::Edge, RestWheelPick::Slots);
    landLeft(moved);
    Run picks = feed(moved, frame(true, false, kCentre, kUp), frames(0.2f));
    picks = feed(moved, frame(true, false, kCentre, kRight), frames(0.2f), picks);
    picks = feed(moved, frame(true, false, kCentre, Axis2{0.0f, -1.0f}), 3, picks);
    picks = feed(moved, frame(true, false, kCentre, kCentre), 2, picks);
    CHECK(picks.slots == std::vector<GameAction>{GameAction::WeaponSlot3});

    // Lifting the thumb picks too.
    RestWheel lift = wheel(RestWheelMode::Edge, RestWheelPick::Slots);
    landLeft(lift);
    feed(lift, frame(true, false, kCentre, Axis2{-1.0f, 0.0f}), frames(0.2f));
    CHECK(feed(lift, frame(false, false, kCentre, Axis2{-1.0f, 0.0f}), 10).slots ==
          std::vector<GameAction>{GameAction::WeaponSlot7});
}

TEST_CASE("weapon by direction: a flick far out picks at once; a half push still needs the dwell") {
    // Two frames at full deflection, then back: shorter than the dwell, but past kFlickThreshold.
    RestWheel flick = wheel(RestWheelMode::Edge, RestWheelPick::Slots);
    landLeft(flick);
    Run run = feed(flick, frame(true, false, kCentre, kRight), 2);
    run = feed(flick, frame(true, false, kCentre, kCentre), 3, run);
    CHECK(run.slots == std::vector<GameAction>{GameAction::WeaponSlot3});
    CHECK(run.pickTicks == 1);

    // The same two frames only half way out press nothing.
    RestWheel half = wheel(RestWheelMode::Edge, RestWheelPick::Slots);
    landLeft(half);
    Run halfRun = feed(half, frame(true, false, kCentre, Axis2{0.6f, 0.0f}), 2);
    halfRun = feed(half, frame(true, false, kCentre, kCentre), 3, halfRun);
    CHECK(halfRun.slots.empty());

    // The weapon wheel keeps its dwell: a flick there opens nothing.
    RestWheel wheelFlick = wheel();
    landLeft(wheelFlick);
    Run wheelRun = feed(wheelFlick, frame(true, false, kCentre, kRight), 2);
    wheelRun = feed(wheelFlick, frame(true, false, kCentre, kCentre), 3, wheelRun);
    CHECK(wheelRun.wheelFrames == 0);
}

TEST_CASE("a direction set to none picks nothing") {
    RestWheelSettings settings;
    settings.pick = RestWheelPick::Slots;
    settings.directions[static_cast<std::size_t>(WheelDirection::Up)] = 0;
    RestWheel w(settings, touchHands());
    landLeft(w);
    Run run = feed(w, frame(true, false, kCentre, kUp), frames(0.3f));
    run = feed(w, frame(true, false, kCentre, kCentre), 3, run);
    CHECK(run.slots.empty());
    CHECK(run.saw(RestWheelEvent::Cancelled));
}

TEST_CASE("blocked: nothing arms, picking is cancelled and an open wheel closes") {
    RestWheel w = wheel();
    landLeft(w);
    CHECK(feed(w, frame(true, false, kCentre, kUp, true), 10).events.empty());

    RestWheel pointing = wheel(RestWheelMode::Edge, RestWheelPick::Slots);
    landLeft(pointing);
    feed(pointing, frame(true, false, kCentre, kUp), frames(0.3f));
    Run cancelled = feed(pointing, frame(true, false, kCentre, kUp, true), 2);
    cancelled = feed(pointing, frame(true, false, kCentre, kCentre), 2, cancelled);
    CHECK(cancelled.saw(RestWheelEvent::Cancelled));
    CHECK(cancelled.slots.empty());

    RestWheel open = wheel();
    landLeft(open);
    feed(open, frame(true, false, kCentre, kUp), frames(0.4f));
    const Run closed = feed(open, frame(true, false, kCentre, kUp, true), frames(0.1f));
    CHECK(closed.saw(RestWheelEvent::Released));
    CHECK_FALSE(closed.last.wheelDown);
}

TEST_CASE("picks in a row: the window opens again once the stick is back in the centre") {
    RestWheel w = wheel();
    landLeft(w);
    feed(w, frame(true, false, kCentre, kUp), frames(0.4f));
    feed(w, frame(true, false, kCentre, kCentre), frames(0.4f));
    CHECK(w.update(frame(true, false, kCentre, kRight), kFrame).event == RestWheelEvent::Armed);

    // Not before the stick has been centred for the re-arm gap.
    RestWheel quick = wheel(RestWheelMode::Edge, RestWheelPick::Slots);
    landLeft(quick);
    feed(quick, frame(true, false, kCentre, kUp), frames(0.2f));
    feed(quick, frame(true, false, kCentre, kCentre), 1);
    CHECK(feed(quick, frame(true, false, kCentre, kUp), 1).events.empty());
}

TEST_CASE("while touched: the other stick is the wheel's for as long as the thumb rests") {
    RestWheel w = wheel(RestWheelMode::Full);
    REQUIRE(w.usable());
    Run resting = feed(w, frame(true, false, kCentre, kCentre), 8);
    CHECK(resting.last.taken[kRightHand]);
    // Long after the touch, a push still picks.
    feed(w, frame(true, false, kCentre, kCentre), frames(2.0f));
    Run run = feed(w, frame(true, false, kCentre, kUp), frames(0.3f));
    CHECK(run.saw(RestWheelEvent::Armed));
    CHECK(run.last.wheelDown);
    run = feed(w, frame(true, false, kCentre, kCentre), frames(0.4f));
    CHECK(run.saw(RestWheelEvent::Released));
    // Still resting: still taken. Lifted: the stick turns again.
    CHECK(run.last.taken[kRightHand]);
    CHECK_FALSE(feed(w, frame(false, false, kCentre, kCentre), 8).last.taken[kRightHand]);
}

TEST_CASE("while touched: a stick already out when the thumb lands waits for the centre") {
    RestWheel w = wheel(RestWheelMode::Full);
    Run run = feed(w, frame(true, false, kCentre, kRight), 20);
    CHECK(run.last.taken[kRightHand]);
    CHECK(run.events.empty());
    feed(w, frame(true, false, kCentre, kCentre), 2);
    CHECK(w.update(frame(true, false, kCentre, kUp), kFrame).event == RestWheelEvent::Armed);
}

TEST_CASE("the turn stick is the wheel: resting the other thumb gives it back") {
    RestWheel w = wheel(RestWheelMode::Extreme);
    REQUIRE(w.usable());
    Run run = feed(w, frame(false, false, kCentre, kCentre), 2);
    CHECK(run.last.taken[kRightHand]);
    CHECK_FALSE(run.last.taken[kLeft]);
    run = feed(w, frame(false, false, kCentre, kUp), frames(0.3f));
    CHECK(run.saw(RestWheelEvent::Armed));
    CHECK(run.last.wheelDown);
    // The left thumb rests: the wheel closes, and the stick turns once it has been back to the centre.
    run = feed(w, frame(true, false, kCentre, kUp), frames(0.4f));
    CHECK(run.saw(RestWheelEvent::Released));
    CHECK(run.last.taken[kRightHand]);
    run = feed(w, frame(true, false, kCentre, kCentre), 2);
    CHECK_FALSE(run.last.taken[kRightHand]);
    CHECK(feed(w, frame(true, false, kCentre, kRight), 20).takenFrames[kRightHand] == 0);
    // Lifted again: the turn stick is the wheel's once more (after coming back to the centre).
    run = feed(w, frame(false, false, kCentre, kRight), 10);
    CHECK(run.last.taken[kRightHand]);
    CHECK_FALSE(run.saw(RestWheelEvent::Armed));
}

TEST_CASE("with both thumbs resting the first push wins") {
    RestWheel w = wheel(RestWheelMode::Full);
    feed(w, frame(true, true, kCentre, kCentre), 8);
    const RestWheelOutput first = w.update(frame(true, true, kCentre, kUp), kFrame);
    CHECK(first.event == RestWheelEvent::Armed);
    const Run other = feed(w, frame(true, true, kUp, kUp), 5);
    CHECK_FALSE(other.saw(RestWheelEvent::Armed));
    CHECK_FALSE(other.last.taken[kLeft]);

    RestWheel edge = wheel();
    feed(edge, frame(true, true, kCentre, kCentre), 8);
    const RestWheelOutput armed = edge.update(frame(true, true, kUp, kCentre), kFrame);
    CHECK(armed.event == RestWheelEvent::Armed);
    CHECK(armed.stickHand == Hand::Left);
    CHECK_FALSE(feed(edge, frame(true, true, kUp, kUp), 5).last.taken[kRightHand]);
}

TEST_CASE("a cancel ends picking, closes an open wheel and forgets the landing") {
    RestWheel w = wheel();
    landLeft(w);
    feed(w, frame(true, false, kCentre, kUp), 3);
    w.cancel();
    Run run = feed(w, frame(true, false, kCentre, kCentre), 3);
    CHECK(run.wheelFrames == 0);
    CHECK(feed(w, frame(true, false, kCentre, kUp), 10).events.empty());

    RestWheel open = wheel();
    landLeft(open);
    feed(open, frame(true, false, kCentre, kUp), frames(0.15f));
    open.cancel();
    run = feed(open, frame(true, false, kCentre, kUp), frames(0.4f));
    CHECK(run.saw(RestWheelEvent::Released));
}

TEST_CASE("the wheel needs a rest on a hand whose other stick has a role") {
    RestWheelHands none = touchHands();
    none.hasRest = {false, false};
    CHECK_FALSE(RestWheel({}, none).usable());
    RestWheelHands noStick = touchHands();
    noStick.moveStick.reset();
    noStick.hasRest = {true, false}; // the left rest picks with the right stick: still usable
    CHECK(RestWheel({}, noStick).usable());
    noStick.hasRest = {false, true}; // the right rest would pick with the left stick, which has no role
    CHECK_FALSE(RestWheel({}, noStick).usable());
    RestWheelSettings extreme;
    extreme.mode = RestWheelMode::Extreme;
    RestWheelHands noTurn = touchHands();
    noTurn.turnStick.reset();
    CHECK_FALSE(RestWheel(extreme, noTurn).usable());
    RestWheelSettings off;
    off.mode = RestWheelMode::Off;
    CHECK_FALSE(RestWheel(off, touchHands()).usable());
    // Without a sensor nothing ever happens.
    RestWheel w(RestWheelSettings{}, none);
    CHECK(feed(w, frame(true, true, kUp, kUp), 20).events.empty());
}

TEST_CASE("a direction on the edge of its eighth is kept until the stick is well past it") {
    RestWheel w = wheel();
    landLeft(w);
    feed(w, frame(true, false, kCentre, kUp), frames(0.2f));
    // 27 degrees right of up is past the edge (22.5) but within the hysteresis: still up.
    const float a = 27.0f * 3.14159265f / 180.0f;
    const RestWheelOutput near =
        w.update(frame(true, false, kCentre, Axis2{std::sin(a), std::cos(a)}), kFrame);
    CHECK(near.wheelDown);
    RestWheel slots = wheel(RestWheelMode::Edge, RestWheelPick::Slots);
    landLeft(slots);
    feed(slots, frame(true, false, kCentre, kUp), frames(0.2f));
    feed(slots, frame(true, false, kCentre, Axis2{std::sin(a), std::cos(a)}), frames(0.2f));
    CHECK(feed(slots, frame(true, false, kCentre, kCentre), 2).slots ==
          std::vector<GameAction>{GameAction::WeaponSlot1});
}

TEST_CASE("a window outside its range falls back to the default") {
    RestWheelSettings settings;
    settings.windowSeconds = 5.0f;
    CHECK(RestWheel(settings, touchHands()).settings().windowSeconds == evr::input::kEdgeWindowSeconds);
    settings.windowSeconds = 0.8f;
    CHECK(RestWheel(settings, touchHands()).settings().windowSeconds == 0.8f);
}

TEST_CASE("a cancel does not open the window again: the next push is the stick's own") {
    RestWheel w = wheel();
    landLeft(w);
    Run run = feed(w, frame(true, false, kCentre, kUp), 5);
    run = feed(w, frame(true, false, kCentre, kCentre), 10, run);
    REQUIRE(run.saw(RestWheelEvent::Cancelled));
    const Run again = feed(w, frame(true, false, kCentre, kUp), 20);
    CHECK(again.events.empty());
    CHECK(again.takenFrames[kRightHand] == 0);
}

TEST_CASE("a thumb that comes straight from its own stick or face button opens no window") {
    // Its own stick out of the centre just before: walking, then the thumb resting.
    RestWheel fromStick = wheel();
    feed(fromStick, frame(false, false, kUp, kCentre), 10);
    feed(fromStick, frame(false, false, kCentre, kCentre), 2);
    const Run landed = feed(fromStick, frame(true, false, kCentre, kCentre), 8);
    CHECK(landed.saw(RestWheelEvent::Voided));
    CHECK(landed.voided == RestWheelVoid::OwnStick);
    CHECK(feed(fromStick, frame(true, false, kCentre, kUp), 20).events.empty());

    // Its own face button let go just before: a jump or a dash, then the thumb resting.
    RestWheel fromButton = wheel();
    RestWheelFrame pressing = frame(false, false, kCentre, kCentre);
    pressing.faceButtons = {true, false};
    feed(fromButton, pressing, 10);
    const Run afterJump = feed(fromButton, frame(true, false, kCentre, kCentre), 8);
    CHECK(afterJump.saw(RestWheelEvent::Voided));
    CHECK(afterJump.voided == RestWheelVoid::OwnButton);
    CHECK(feed(fromButton, frame(true, false, kCentre, kUp), 20).events.empty());

    // The other hand's button or stick does not count; nor does its own after the void time.
    RestWheel other = wheel();
    RestWheelFrame otherButton = frame(false, false, kCentre, kCentre);
    otherButton.faceButtons = {false, true};
    feed(other, otherButton, 10);
    landLeft(other);
    CHECK(other.update(frame(true, false, kCentre, kUp), kFrame).event == RestWheelEvent::Armed);
    RestWheel idle = wheel();
    feed(idle, pressing, 10);
    feed(idle, frame(false, false, kCentre, kCentre), frames(0.45f));
    landLeft(idle);
    CHECK(idle.update(frame(true, false, kCentre, kUp), kFrame).event == RestWheelEvent::Armed);
}

TEST_CASE("the wheel's hold follows the game's open delay") {
    using evr::input::wheelHoldSeconds;
    CHECK(wheelHoldSeconds(std::nullopt) == kMinWheelHoldSeconds);
    CHECK(wheelHoldSeconds(180.0f) == kMinWheelHoldSeconds);
    CHECK(wheelHoldSeconds(500.0f) == doctest::Approx(0.62f));
    CHECK(wheelHoldSeconds(0.0f) == kMinWheelHoldSeconds);
    CHECK(wheelHoldSeconds(-5.0f) == kMinWheelHoldSeconds);
    CHECK(wheelHoldSeconds(std::nanf("")) == kMinWheelHoldSeconds);
    CHECK(wheelHoldSeconds(10000.0f) == evr::input::kMaxWheelHoldSeconds);

    RestWheel w = wheel();
    landLeft(w);
    RestWheelFrame up = frame(true, false, kCentre, kUp);
    up.minWheelHoldSeconds = 0.62f;
    Run run = feed(w, up, frames(0.12f));
    REQUIRE(run.saw(RestWheelEvent::Opened));
    RestWheelFrame centre = frame(true, false, kCentre, kCentre);
    centre.minWheelHoldSeconds = 0.62f;
    run = feed(w, centre, frames(0.8f), run);
    CHECK(static_cast<float>(run.wheelFrames) * kFrame >= 0.62f - kFrame);
    CHECK(run.saw(RestWheelEvent::Released));
}
