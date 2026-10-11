// The thumb-rest wheel's timing across a gap in the rest's touch (DECISIONS T-120): the push given back to
// the game on a lift to the thumb's own controls and the flick held while touched, timed from the frame each
// was first seen, at any frame times, a push exactly at the limit inside it; and a push of the move stick
// held during a hold-over, played to the movement when the touch lifts.

#include "features/input/input_frames.hpp"
#include "features/input/input_mapper.hpp"
#include "features/input/rest_wheel.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <vector>

using evr::input::Axis2;
using evr::input::GameInput;
using evr::input::Hand;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::magnitude;
using evr::input::MapperSettings;
using evr::input::RestWheel;
using evr::input::RestWheelEvent;
using evr::input::RestWheelFrame;
using evr::input::RestWheelHands;
using evr::input::RestWheelMode;
using evr::input::RestWheelOutput;
using evr::input::RestWheelSettings;
using evr::test::questTouchProfile;
using evr::test::restingFrame;

namespace {

constexpr float kFrame = 1.0f / 90.0f;
constexpr Axis2 kCentre{};
constexpr Axis2 kUp{0.0f, 1.0f};
constexpr Axis2 kRight{1.0f, 0.0f};
constexpr int kLeft = 0;
constexpr int kMoveHand = 0;
constexpr int kTurnHand = 1;

RestWheelFrame frame(bool leftRest, Axis2 left, Axis2 right) {
    RestWheelFrame f;
    f.rest = {leftRest, false};
    f.sticks = {left, right};
    return f;
}

RestWheel fullWheel() {
    RestWheelHands hands;
    hands.hasRest = {true, true};
    hands.moveStick = Hand::Left;
    hands.turnStick = Hand::Right;
    RestWheelSettings settings;
    settings.mode = RestWheelMode::Full;
    return RestWheel(settings, hands);
}

// `count` frames of `seconds` each.
std::vector<float> times(int count, float seconds) {
    return std::vector<float>(static_cast<std::size_t>(count), seconds);
}

// While touched, the left thumb resting: the turn stick pushed right on a frame `pushed[0]` long, the rest
// still touched for the frames after it (`pushed`), then lost for `flicker` (frames off and back on), then
// the thumb on its own stick on a frame `cut` long, which lifts the rest at once and cancels the pick.
RestWheelOutput
liftAfterPush(const std::vector<float>& pushed, const std::vector<float>& flicker, float cut) {
    RestWheel w = fullWheel();
    for (int i = 0; i < 30; ++i) {
        w.update(frame(true, kCentre, kCentre), kFrame);
    }
    for (const float dt : pushed) {
        w.update(frame(true, kCentre, kRight), dt);
    }
    bool on = false;
    for (const float dt : flicker) {
        w.update(frame(on, kCentre, kRight), dt);
        on = !on;
    }
    return w.update(frame(false, kUp, kRight), cut);
}

// Given back: the push was a turn, centred to the gestures on this frame so they claim it anew, and the
// game's after. Kept: out of play until it is back in the centre.
bool givenBack(const RestWheelOutput& out) {
    REQUIRE(out.event == RestWheelEvent::Cancelled);
    CHECK(out.taken[kTurnHand]);
    return out.deferred[kTurnHand];
}

// While touched, no thumb resting: the turn stick pushed right on the first of the frames `pushed`, then the
// left thumb lands on a frame `landing` long. Whether the push is held and picks (it began at most
// kHeldLeadSeconds before the touch) or is left to the game (a turn).
bool heldAtLanding(const std::vector<float>& pushed, float landing) {
    RestWheel w = fullWheel();
    for (int i = 0; i < 4; ++i) {
        w.update(frame(false, kCentre, kCentre), kFrame);
    }
    for (const float dt : pushed) {
        w.update(frame(false, kCentre, kRight), dt);
    }
    const RestWheelOutput out = w.update(frame(true, kCentre, kRight), landing);
    CHECK(out.taken[kTurnHand] == (w.stats().heldPushes[kLeft] == 1));
    return out.taken[kTurnHand];
}

MapperSettings restSettings(RestWheelMode mode) {
    MapperSettings settings;
    settings.restWheel.mode = mode;
    settings.restSensors = {true, true};
    return settings;
}

InputFrame mapperFrame(bool rightRest, Axis2 left) {
    InputFrame f = restingFrame();
    f.right.thumbRest = rightRest;
    f.left.stick = left;
    return f;
}

struct Moves {
    int moving = 0;   // frames that moved
    int deferred = 0; // frames that held the move stick back
    int replays = 0;
    float moved = 0.0f; // the largest move magnitude
    bool wheel = false;
    bool lastMoving = false;
};

// The right thumb rests (the move stick is the picker), then its touch drops out for good and the move stick
// is pushed up for two frames, then left at `after` (centred: a tap). `mixed`: frames of 1/72 s and 1/120 s
// in turn instead of 1/90 s.
Moves pushDuringHoldOver(RestWheelMode mode, Axis2 after, bool mixed) {
    InputMapper mapper(questTouchProfile(), restSettings(mode));
    int frameIndex = 0;
    const auto step = [&](bool rest, Axis2 stick) {
        const float dt = mixed ? (frameIndex % 2 == 0 ? 1.0f / 72.0f : 1.0f / 120.0f) : kFrame;
        ++frameIndex;
        return mapper.update(mapperFrame(rest, stick), {}, dt);
    };
    for (int i = 0; i < 45; ++i) {
        step(false, kCentre);
    }
    for (int i = 0; i < 20; ++i) {
        step(true, kCentre);
    }
    for (int i = 0; i < 3; ++i) {
        step(false, kCentre);
    }
    Moves moves;
    for (int i = 0; i < 32; ++i) {
        const GameInput input = step(false, i < 2 ? kUp : after);
        const bool moving = magnitude(input.move) > 0.0f;
        moves.moving += moving ? 1 : 0;
        moves.deferred += input.restWheel.deferred[kMoveHand] ? 1 : 0;
        moves.replays += input.restWheel.replay[kMoveHand] ? 1 : 0;
        moves.moved = std::fmax(moves.moved, magnitude(input.move));
        moves.wheel =
            moves.wheel || input.restWheel.wheelDown || input.restWheel.event == RestWheelEvent::Armed;
        moves.lastMoving = moving;
        CHECK_FALSE((moving && input.restWheel.deferred[kMoveHand]));
    }
    return moves;
}

} // namespace

TEST_CASE("a lift on the thumb's own controls gives back a push begun up to 0.05 s before the touch first "
          "went, at any frame times") {
    // 90 Hz: the touch gone four frames after the push (0.044 s) is a turn; five frames (0.056 s) a pick.
    CHECK(givenBack(liftAfterPush(times(4, kFrame), {}, kFrame)));
    CHECK_FALSE(givenBack(liftAfterPush(times(5, kFrame), {}, kFrame)));

    // Uneven frames: the push on a 14 ms frame, the touch gone 0.044 s later; 0.052 s later, a pick. The
    // frame the push was first seen on does not count: it passed before the stick was out.
    CHECK(givenBack(liftAfterPush({0.014f, 0.011f, 0.008f, 0.014f}, {}, 0.011f)));
    CHECK_FALSE(givenBack(liftAfterPush({0.014f, 0.011f, 0.008f, 0.014f, 0.008f}, {}, 0.011f)));

    // Timed from when the touch first went: gone 0.044 s after the push, back for a frame, then the thumb on
    // its own stick.
    CHECK(givenBack(liftAfterPush({0.014f, 0.011f, 0.011f, 0.011f}, {0.011f, 0.008f}, 0.008f)));
    CHECK_FALSE(givenBack(liftAfterPush({0.014f, 0.011f, 0.011f, 0.011f, 0.008f}, {0.011f, 0.008f}, 0.008f)));
}

TEST_CASE("exactly 0.05 s between the push and the touch going is still a turn, however the frames sum") {
    // 2 x 0.025 s, 3 frames at 60 Hz (summed, a rounding step over 0.05 s), 4 at 80 Hz; one frame more is a
    // pick.
    for (const float dt : {0.025f, 1.0f / 60.0f, 1.0f / 80.0f}) {
        CAPTURE(dt);
        const int count = static_cast<int>(std::lround(0.05f / dt));
        CHECK(givenBack(liftAfterPush(times(count, dt), {}, dt)));
        CHECK_FALSE(givenBack(liftAfterPush(times(count + 1, dt), {}, dt)));
    }
}

TEST_CASE("while touched: a flick exactly 0.1 s before the touch is held, however the frames sum") {
    // 9 frames at 90 Hz, 4 x 0.025 s, 8 frames at 80 Hz (summed, a rounding step over 0.1 s), 6 at 60 Hz;
    // one frame more is a turn, left to the game.
    for (const float dt : {kFrame, 0.025f, 1.0f / 80.0f, 1.0f / 60.0f}) {
        CAPTURE(dt);
        const int count = static_cast<int>(std::lround(0.1f / dt));
        CHECK(heldAtLanding(times(count, dt), dt));
        CHECK_FALSE(heldAtLanding(times(count + 1, dt), dt));
    }
}

TEST_CASE("a tap of the move stick during the other rest's hold-over is played for one frame when the touch "
          "lifts") {
    for (const bool mixed : {false, true}) {
        for (const RestWheelMode mode : {RestWheelMode::Edge, RestWheelMode::Full, RestWheelMode::Extreme}) {
            CAPTURE(mixed);
            CAPTURE(static_cast<int>(mode));
            // Held while the touch decides; when the hold-over lapses the tap moves for one frame at its
            // furthest point.
            const Moves moves = pushDuringHoldOver(mode, kCentre, mixed);
            CHECK(moves.deferred >= 2);
            CHECK(moves.moving == 1);
            CHECK(moves.replays == 1);
            CHECK(moves.moved > 0.5f);
            CHECK_FALSE(moves.lastMoving);
            CHECK_FALSE(moves.wheel);
        }
    }
}

TEST_CASE("a move-stick push still at its furthest when the touch lifts moves live, with nothing replayed") {
    for (const RestWheelMode mode : {RestWheelMode::Edge, RestWheelMode::Full, RestWheelMode::Extreme}) {
        CAPTURE(static_cast<int>(mode));
        const Moves moves = pushDuringHoldOver(mode, kUp, false);
        CHECK(moves.replays == 0);
        CHECK(moves.deferred >= 2);
        CHECK(moves.moving == 32 - moves.deferred);
        CHECK(moves.lastMoving);
        CHECK_FALSE(moves.wheel);
    }
}
