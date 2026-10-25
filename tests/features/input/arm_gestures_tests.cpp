#include "features/input/arm_gestures.hpp"

#include "features/input/input_frames.hpp"
#include "features/input/input_mapper.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <ostream>

using evr::game::contains;
using evr::game::GameAction;
using evr::input::ArmGestureOutput;
using evr::input::ArmGestures;
using evr::input::Hand;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::MapperSettings;
using evr::input::SwingSettings;
using evr::test::questTouchProfile;
using evr::test::restingFrame;
using evr::test::trackedHand;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

SwingSettings swingOn() {
    SwingSettings s;
    s.enabled = true;
    return s;
}

// The resting frame (head at 1.7 m facing -Z), the left hand beside the left ear, a little behind the eyes.
InputFrame leftWoundUp() {
    InputFrame frame = restingFrame();
    frame.left = trackedHand({-0.15f, 1.75f, 0.05f});
    return frame;
}

// The left hand swinging forward past the face (the removed throw gesture's motion).
InputFrame leftThrowing(float forwardSpeed = 3.0f) {
    InputFrame frame = restingFrame();
    frame.left = trackedHand({-0.15f, 1.65f, -0.3f}, {0.0f, -0.5f, -forwardSpeed});
    return frame;
}

InputFrame rightRaised() {
    InputFrame frame = restingFrame();
    frame.right = trackedHand({0.2f, 1.95f, -0.1f});
    return frame;
}

InputFrame rightChopping(float downSpeed = 3.0f) {
    InputFrame frame = restingFrame();
    frame.right = trackedHand({0.2f, 1.6f, -0.3f}, {0.0f, -downSpeed, -1.0f});
    return frame;
}

ArmGestureOutput run(ArmGestures& gestures, const InputFrame& frame, Hand weaponHand = Hand::Right) {
    return gestures.update(frame, weaponHand, kFrame);
}

} // namespace

TEST_CASE("the swing is off by default") {
    ArmGestures gestures;
    CHECK_FALSE(gestures.swingSettings().enabled);
    run(gestures, rightRaised());
    CHECK_FALSE(run(gestures, rightChopping()).swung);
}

TEST_CASE("the weapon hand raised above the head, then brought down hard, swings") {
    ArmGestures gestures(swingOn());
    const ArmGestureOutput raised = run(gestures, rightRaised());
    CHECK(raised.heldBack[1]);
    CHECK_FALSE(raised.heldBack[0]);
    const ArmGestureOutput chop = run(gestures, rightChopping());
    CHECK(chop.swung);
    CHECK_FALSE(run(gestures, rightChopping()).swung);
    // Below the default speed (2.5 m/s) it does not.
    for (int i = 0; i < 50; ++i) {
        run(gestures, restingFrame());
    }
    run(gestures, rightRaised());
    CHECK_FALSE(run(gestures, rightChopping(2.3f)).swung);
    CHECK(run(gestures, rightChopping(2.7f)).swung);
}

TEST_CASE("a downward motion that did not start above the head does not swing") {
    ArmGestures gestures(swingOn());
    run(gestures, restingFrame());
    CHECK_FALSE(run(gestures, rightChopping(4.0f)).swung);
    // The off hand raised and brought down never swings.
    InputFrame leftUp = restingFrame();
    leftUp.left = trackedHand({-0.2f, 1.95f, -0.1f});
    InputFrame leftDown = restingFrame();
    leftDown.left = trackedHand({-0.2f, 1.6f, -0.3f}, {0.0f, -3.0f, 0.0f});
    run(gestures, leftUp);
    CHECK_FALSE(run(gestures, leftDown).swung);
}

TEST_CASE("both hands up (a stretch, a hands jump) does not swing until the weapon hand came down") {
    ArmGestures gestures(swingOn());
    InputFrame both = rightRaised();
    both.left = trackedHand({-0.2f, 1.95f, -0.1f});
    const ArmGestureOutput up = run(gestures, both);
    CHECK_FALSE(up.heldBack[1]);
    // The off hand comes down first, the weapon hand stays up a moment, then drops fast.
    run(gestures, rightRaised());
    CHECK_FALSE(run(gestures, rightChopping()).swung);
    // Raised on its own later: the swing works again.
    for (int i = 0; i < 50; ++i) {
        run(gestures, restingFrame());
    }
    run(gestures, rightRaised());
    CHECK(run(gestures, rightChopping()).swung);
}

TEST_CASE("tuning that is not finite falls back; speeds are clamped to 1-5 m/s") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    SwingSettings s = swingOn();
    s.speed = 0.2f;
    s.raiseMinHeight = nan;
    s.primeSeconds = -1.0f;
    const ArmGestures gestures(s);
    CHECK(gestures.swingSettings().primeSeconds == SwingSettings{}.primeSeconds);
    CHECK(gestures.swingSettings().speed == evr::input::kMinGestureSpeed);
    CHECK(gestures.swingSettings().raiseMinHeight == SwingSettings{}.raiseMinHeight);
}

TEST_CASE("mapper: an overhand throw of the off hand is a punch: there is no throw gesture") {
    InputMapper mapper(questTouchProfile());
    mapper.update(restingFrame(), {}, kFrame);
    mapper.update(leftWoundUp(), {}, kFrame);
    const auto input = mapper.update(leftThrowing(3.5f), {}, kFrame);
    CHECK(contains(input.pressed, GameAction::Melee));
    CHECK_FALSE(contains(input.down, GameAction::Equipment));
}

TEST_CASE("mapper: the overhead swing presses the Crucible and does not punch") {
    MapperSettings settings;
    settings.swing.enabled = true;
    InputMapper mapper(questTouchProfile(), settings);
    mapper.update(restingFrame(), {}, kFrame);
    mapper.update(rightRaised(), {}, kFrame);
    InputFrame chop = rightChopping();
    chop.right.linearVelocity = {0.0f, -3.0f, -3.0f}; // forward fast enough to punch as well
    const auto input = mapper.update(chop, {}, kFrame);
    CHECK(contains(input.pressed, GameAction::Crucible));
    CHECK_FALSE(contains(input.down, GameAction::Melee));
    CHECK(input.swung);
}
