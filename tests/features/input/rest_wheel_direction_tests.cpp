// The thumb-rest wheel's direction: the pointer and the edge of an eighth, and weapon by direction
// (RestWheelPick::Slots): the slot of the direction held is pressed on letting go, a flick far out picks at
// once, and a direction set to none goes to its nearest neighbours.

#include "features/input/rest_wheel.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <ostream>
#include <vector>

using evr::game::GameAction;
using evr::input::Axis2;
using evr::input::Hand;
using evr::input::kRestDwellSeconds;
using evr::input::parseWeaponDirections;
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

// The left thumb rests (long enough to settle, kRestDwellSeconds), both sticks centred.
void landLeft(RestWheel& w) {
    feed(w, frame(true, false, kCentre, kCentre), frames(kRestDwellSeconds) + 1);
}

} // namespace

TEST_CASE("the pointer keeps the last direction while the stick falls back toward the centre") {
    RestWheel w = wheel();
    landLeft(w);
    feed(w, frame(true, false, kCentre, kRight), frames(0.4f));
    const RestWheelOutput inside = w.update(frame(true, false, kCentre, Axis2{0.0f, 0.4f}), kFrame);
    CHECK(inside.wheelDown);
    CHECK(inside.pointer == kRight);
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
    CHECK(picks.slots == std::vector<GameAction>{GameAction::WeaponSlot2});

    // Lifting the thumb picks nothing: letting go of the stick does.
    RestWheel lift = wheel(RestWheelMode::Edge, RestWheelPick::Slots);
    landLeft(lift);
    feed(lift, frame(true, false, kCentre, Axis2{-1.0f, 0.0f}), frames(0.2f));
    CHECK(feed(lift, frame(false, false, kCentre, Axis2{-1.0f, 0.0f}), frames(0.3f)).slots.empty());
    CHECK(feed(lift, frame(false, false, kCentre, kCentre), 2).slots ==
          std::vector<GameAction>{GameAction::WeaponSlot4});
}

TEST_CASE("weapon by direction: a flick far out picks at once; a half push still needs the dwell") {
    // Two frames at full deflection, then back: shorter than the dwell, but past kFlickThreshold.
    RestWheel flick = wheel(RestWheelMode::Edge, RestWheelPick::Slots);
    landLeft(flick);
    Run run = feed(flick, frame(true, false, kCentre, kRight), 2);
    run = feed(flick, frame(true, false, kCentre, kCentre), 3, run);
    CHECK(run.slots == std::vector<GameAction>{GameAction::WeaponSlot2});
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

TEST_CASE("weapon by direction: a flick that curves on its way out picks the eighth it reaches") {
    // Crosses the select threshold up-right (34 degrees), then goes out to the right (21 and 11 degrees): the
    // hysteresis alone would have kept up-right.
    RestWheel flick = wheel(RestWheelMode::Edge, RestWheelPick::Slots);
    landLeft(flick);
    Run run = feed(flick, frame(true, false, kCentre, Axis2{0.45f, 0.3f}), 1);
    run = feed(flick, frame(true, false, kCentre, Axis2{0.8f, 0.3f}), 1, run);
    run = feed(flick, frame(true, false, kCentre, Axis2{0.98f, 0.2f}), 1, run);
    run = feed(flick, frame(true, false, kCentre, kCentre), 3, run);
    CHECK(run.slots == std::vector<GameAction>{GameAction::WeaponSlot2});

    // The same path held at the rim: the dwell takes the eighth the stick ended in.
    RestWheel held = wheel(RestWheelMode::Edge, RestWheelPick::Slots);
    landLeft(held);
    Run heldRun = feed(held, frame(true, false, kCentre, Axis2{0.45f, 0.3f}), 1);
    heldRun = feed(held, frame(true, false, kCentre, Axis2{0.98f, 0.2f}), frames(0.3f), heldRun);
    heldRun = feed(held, frame(true, false, kCentre, kCentre), 3, heldRun);
    CHECK(heldRun.slots == std::vector<GameAction>{GameAction::WeaponSlot2});

    // At the rim, sliding a little along it (within the hysteresis) keeps the direction.
    const float a = 27.0f * 3.14159265f / 180.0f;
    RestWheel slide = wheel(RestWheelMode::Edge, RestWheelPick::Slots);
    landLeft(slide);
    Run slideRun = feed(slide, frame(true, false, kCentre, kRight), 2);
    slideRun = feed(slide, frame(true, false, kCentre, Axis2{std::cos(a), std::sin(a)}), 2, slideRun);
    slideRun = feed(slide, frame(true, false, kCentre, kCentre), 3, slideRun);
    CHECK(slideRun.slots == std::vector<GameAction>{GameAction::WeaponSlot2});
}

TEST_CASE("a direction set to none goes to its nearest neighbours; with none set nothing is picked") {
    // Up, right, down and left only: 35 degrees right of up (up-right's eighth) picks up, held or flicked.
    RestWheelSettings settings;
    settings.pick = RestWheelPick::Slots;
    settings.directions = parseWeaponDirections("up_right=0,down_right=0,down_left=0,up_left=0").table;
    const auto at = [](float degrees, float length) {
        const float a = degrees * 3.14159265f / 180.0f;
        return Axis2{length * std::cos(a), length * std::sin(a)};
    };
    for (const int outFrames : {frames(0.3f), 2}) {
        RestWheel w(settings, touchHands());
        landLeft(w);
        Run run = feed(w, frame(true, false, kCentre, at(55.0f, 1.0f)), outFrames);
        run = feed(w, frame(true, false, kCentre, kCentre), 1, run);
        CHECK(run.slots == std::vector<GameAction>{GameAction::WeaponSlot1});
        CHECK(run.last.direction == WheelDirection::Up);
    }
    // A half push sliding from up's eighth into up-right's stays up: the dwell goes on.
    RestWheel slide(settings, touchHands());
    landLeft(slide);
    Run slideRun = feed(slide, frame(true, false, kCentre, at(100.0f, 0.6f)), 6);
    slideRun = feed(slide, frame(true, false, kCentre, at(55.0f, 0.6f)), 6, slideRun);
    slideRun = feed(slide, frame(true, false, kCentre, kCentre), 3, slideRun);
    CHECK(slideRun.slots == std::vector<GameAction>{GameAction::WeaponSlot1});

    settings.directions = {};
    RestWheel none(settings, touchHands());
    landLeft(none);
    Run run = feed(none, frame(true, false, kCentre, kUp), frames(0.3f));
    run = feed(none, frame(true, false, kCentre, kCentre), 3, run);
    CHECK(run.slots.empty());
    CHECK(run.saw(RestWheelEvent::Cancelled));
}
