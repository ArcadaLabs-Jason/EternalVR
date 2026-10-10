// The thumb-rest wheel's turn-stick mode (extreme): for standing players who turn with their body.

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
constexpr int kLeft = 0;
constexpr int kRightHand = 1;

RestWheel wheel(RestWheelPick pick = RestWheelPick::Wheel) {
    RestWheelHands hands;
    hands.hasRest = {true, true};
    hands.moveStick = Hand::Left;
    hands.turnStick = Hand::Right;
    RestWheelSettings settings;
    settings.mode = RestWheelMode::Extreme;
    settings.pick = pick;
    return RestWheel(settings, hands);
}

RestWheelFrame frame(bool leftRest, bool rightRest, Axis2 left, Axis2 right, bool blocked = false) {
    RestWheelFrame f;
    f.rest = {leftRest, rightRest};
    f.sticks = {left, right};
    f.blocked = blocked;
    return f;
}

struct Run {
    std::vector<RestWheelEvent> events;
    int wheelFrames = 0;
    int takenFrames[2] = {0, 0};
    std::vector<GameAction> slots;
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
        run.wheelFrames += out.wheelDown ? 1 : 0;
        for (int h = 0; h < 2; ++h) {
            run.takenFrames[h] += out.taken[h] ? 1 : 0;
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

} // namespace

TEST_CASE("the turn stick is the wheel: resting the other thumb gives it back") {
    RestWheel w = wheel();
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

TEST_CASE("the turn hand's thumb resting makes the other stick the picker, and it does not walk") {
    RestWheel w = wheel(RestWheelPick::Slots);
    feed(w, frame(false, false, kCentre, kCentre), 2);
    // The right thumb rests: the right stick is free (the thumb is off it), the left stick is the wheel's.
    Run run = feed(w, frame(false, true, kCentre, kCentre), 8);
    CHECK(run.last.taken[kLeft]);
    CHECK_FALSE(run.last.taken[kRightHand]);
    const RestWheelOutput armed = w.update(frame(false, true, kUp, kCentre), kFrame);
    CHECK(armed.event == RestWheelEvent::Armed);
    CHECK(armed.stickHand == Hand::Left);
    CHECK(armed.restHand == Hand::Right);
    run = feed(w, frame(false, true, kUp, kCentre), frames(0.2f));
    CHECK(run.takenFrames[kLeft] == frames(0.2f));
    run = feed(w, frame(false, true, kCentre, kCentre), 2);
    CHECK(run.saw(RestWheelEvent::Picked));
    REQUIRE(run.slots.size() == 1);
    // The right thumb lifted: the turn stick is the wheel's again, the left stick walks.
    run = feed(w, frame(false, false, kCentre, kCentre), 8);
    CHECK(run.last.taken[kRightHand]);
    CHECK_FALSE(run.last.taken[kLeft]);
}

TEST_CASE("the other thumb landing in the middle of a pick cancels it") {
    RestWheel w = wheel(RestWheelPick::Slots);
    feed(w, frame(false, false, kCentre, kCentre), 2);
    // A flick commits the direction; the left thumb then lands with the stick still out: no weapon.
    Run run = feed(w, frame(false, false, kCentre, kRight), frames(0.15f));
    CHECK(run.saw(RestWheelEvent::Armed));
    run = feed(w, frame(true, false, kCentre, kRight), frames(0.15f));
    CHECK(run.saw(RestWheelEvent::Cancelled));
    CHECK_FALSE(run.saw(RestWheelEvent::Picked));
    CHECK(run.slots.empty());
    // The stick turns once it has been back in the centre.
    feed(w, frame(true, false, kCentre, kCentre), 2);
    CHECK(feed(w, frame(true, false, kCentre, kRight), 10).takenFrames[kRightHand] == 0);
}

TEST_CASE("blocked (piloting a demon, a cutscene) gives the turn stick back to the game") {
    RestWheel w = wheel();
    CHECK(feed(w, frame(false, false, kCentre, kCentre), 2).last.taken[kRightHand]);
    Run run = feed(w, frame(false, false, kCentre, kCentre, true), 2);
    CHECK_FALSE(run.last.taken[kRightHand]);
    run = feed(w, frame(false, false, kCentre, kRight, true), 20);
    CHECK(run.takenFrames[kRightHand] == 0);
    CHECK(run.events.empty());
    // Play is back with the stick still out: it is the wheel's again, but picks nothing until it has been
    // back in the centre.
    run = feed(w, frame(false, false, kCentre, kRight), 10);
    CHECK(run.last.taken[kRightHand]);
    CHECK(run.events.empty());
    feed(w, frame(false, false, kCentre, kCentre), 2);
    CHECK(w.update(frame(false, false, kCentre, kUp), kFrame).event == RestWheelEvent::Armed);
}

TEST_CASE("with both thumbs resting both sticks are the game's") {
    RestWheel w = wheel();
    feed(w, frame(true, true, kCentre, kCentre), 10);
    const Run run = feed(w, frame(true, true, kUp, kRight), 20);
    CHECK(run.takenFrames[kLeft] == 0);
    CHECK(run.takenFrames[kRightHand] == 0);
    CHECK(run.events.empty());
}
