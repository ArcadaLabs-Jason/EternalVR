#include "features/input/arm_gestures.hpp"

#include "features/input/input_frames.hpp"
#include "features/input/input_mapper.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <numbers>
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
using evr::input::ThrowSettings;
using evr::test::questTouchProfile;
using evr::test::restingFrame;
using evr::test::trackedHand;
using evr::test::yawPose;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

ThrowSettings throwOn() {
    ThrowSettings s;
    s.enabled = true;
    return s;
}

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

// The left hand swinging forward past the face.
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

TEST_CASE("both gestures are off by default") {
    ArmGestures gestures;
    CHECK_FALSE(gestures.throwSettings().enabled);
    CHECK_FALSE(gestures.swingSettings().enabled);
    run(gestures, leftWoundUp());
    CHECK_FALSE(run(gestures, leftThrowing()).thrown);
    run(gestures, rightRaised());
    CHECK_FALSE(run(gestures, rightChopping()).swung);
}

TEST_CASE("the off hand wound up beside the head, then swung forward, throws") {
    ArmGestures gestures(throwOn());
    const ArmGestureOutput windup = run(gestures, leftWoundUp());
    CHECK_FALSE(windup.thrown);
    CHECK(windup.heldBack[0]); // primed: the left hand's punch is held back
    CHECK_FALSE(windup.heldBack[1]);
    const ArmGestureOutput thrown = run(gestures, leftThrowing());
    CHECK(thrown.thrown);
    CHECK(thrown.heldBack[0]);
    CHECK_FALSE(thrown.swung);
}

TEST_CASE("a forward swing from the chest is a punch, not a throw") {
    ArmGestures gestures(throwOn());
    run(gestures, restingFrame());
    InputFrame jab = restingFrame();
    jab.left = trackedHand({-0.2f, 1.3f, -0.4f}, {0.0f, 0.0f, -3.5f});
    const ArmGestureOutput out = run(gestures, jab);
    CHECK_FALSE(out.thrown);
    CHECK_FALSE(out.heldBack[0]);
    // A hand raised in front of the face (a guard, or steadying the weapon) is not wound up either.
    InputFrame guard = restingFrame();
    guard.left = trackedHand({-0.1f, 1.65f, -0.25f});
    run(gestures, guard);
    CHECK_FALSE(run(gestures, leftThrowing()).thrown);
}

TEST_CASE("the throw has to follow the wind-up within the prime time") {
    ArmGestures gestures(throwOn());
    run(gestures, leftWoundUp());
    // The hand comes down slowly and waits past the prime time (0.8 s).
    for (int i = 0; i < 90; ++i) {
        run(gestures, restingFrame());
    }
    const ArmGestureOutput late = run(gestures, leftThrowing());
    CHECK_FALSE(late.thrown);
    CHECK_FALSE(late.heldBack[0]);
    // Within it, the throw fires.
    run(gestures, leftWoundUp());
    for (int i = 0; i < 30; ++i) {
        run(gestures, restingFrame());
    }
    CHECK(run(gestures, leftThrowing()).thrown);
}

TEST_CASE("one throw per wind-up, and none within the cooldown") {
    ArmGestures gestures(throwOn());
    run(gestures, leftWoundUp());
    CHECK(run(gestures, leftThrowing()).thrown);
    CHECK_FALSE(run(gestures, leftThrowing(4.0f)).thrown);
    // Wound up again at once: still in the cooldown (0.5 s).
    run(gestures, leftWoundUp());
    CHECK_FALSE(run(gestures, leftThrowing()).thrown);
    for (int i = 0; i < 50; ++i) {
        run(gestures, restingFrame());
    }
    run(gestures, leftWoundUp());
    CHECK(run(gestures, leftThrowing()).thrown);
}

TEST_CASE("the throw needs the default forward speed of 2 m/s") {
    ArmGestures gestures(throwOn());
    run(gestures, leftWoundUp());
    CHECK_FALSE(run(gestures, leftThrowing(1.8f)).thrown);
    CHECK(run(gestures, leftThrowing(2.2f)).thrown);
}

TEST_CASE("only the off hand throws; with the weapon in the left hand it is the right one") {
    ArmGestures gestures(throwOn());
    InputFrame rightWoundUp = restingFrame();
    rightWoundUp.right = trackedHand({0.15f, 1.75f, 0.05f});
    InputFrame rightThrowing = restingFrame();
    rightThrowing.right = trackedHand({0.15f, 1.65f, -0.3f}, {0.0f, 0.0f, -3.0f});
    run(gestures, rightWoundUp);
    CHECK_FALSE(run(gestures, rightThrowing).thrown);

    run(gestures, rightWoundUp, Hand::Left);
    CHECK(run(gestures, rightThrowing, Hand::Left).thrown);
    run(gestures, leftWoundUp(), Hand::Left);
    CHECK_FALSE(run(gestures, leftThrowing(), Hand::Left).thrown);
}

TEST_CASE("the throw is measured in the head's heading") {
    ArmGestures gestures(throwOn());
    // Facing -X: the wind-up is behind the eyes along -X, the throw moves along -X.
    InputFrame windup = restingFrame();
    windup.head.pose = yawPose(std::numbers::pi_v<float> / 2.0f, windup.head.pose.position);
    InputFrame throwing = windup;
    windup.left = trackedHand({0.05f, 1.75f, 0.15f});
    throwing.left = trackedHand({-0.3f, 1.65f, 0.15f}, {-3.0f, 0.0f, 0.0f});
    run(gestures, windup);
    CHECK(run(gestures, throwing).thrown);
    // Looking straight down, the heading comes from the head's up direction: still -X.
    InputFrame down = windup;
    down.head.pose.orientation =
        yawPose(std::numbers::pi_v<float> / 2.0f).orientation *
        evr::Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, -std::numbers::pi_v<float> / 2.0f);
    for (int i = 0; i < 50; ++i) {
        run(gestures, restingFrame());
    }
    InputFrame downThrowing = throwing;
    downThrowing.head = down.head;
    run(gestures, down);
    CHECK(run(gestures, downThrowing).thrown);
}

TEST_CASE("a hand whose tracking is lost after the wind-up has to wind up again") {
    ArmGestures gestures(throwOn());
    run(gestures, leftWoundUp());
    InputFrame lost = restingFrame();
    lost.left.poseValid = false;
    run(gestures, lost);
    CHECK_FALSE(run(gestures, leftThrowing()).thrown);
    InputFrame noHead = leftWoundUp();
    noHead.head.poseValid = false;
    run(gestures, noHead);
    CHECK_FALSE(run(gestures, leftThrowing()).thrown);
}

TEST_CASE("the weapon hand raised above the head, then brought down hard, swings") {
    ArmGestures gestures({}, swingOn());
    const ArmGestureOutput raised = run(gestures, rightRaised());
    CHECK(raised.heldBack[1]);
    CHECK_FALSE(raised.heldBack[0]);
    const ArmGestureOutput chop = run(gestures, rightChopping());
    CHECK(chop.swung);
    CHECK_FALSE(chop.thrown);
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
    ArmGestures gestures({}, swingOn());
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
    ArmGestures gestures({}, swingOn());
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
    ThrowSettings t = throwOn();
    t.speed = nan;
    t.primeSeconds = -1.0f;
    SwingSettings s = swingOn();
    s.speed = 0.2f;
    s.raiseMinHeight = nan;
    const ArmGestures gestures(t, s);
    CHECK(gestures.throwSettings().speed == ThrowSettings{}.speed);
    CHECK(gestures.throwSettings().primeSeconds == ThrowSettings{}.primeSeconds);
    CHECK(gestures.swingSettings().speed == evr::input::kMinGestureSpeed);
    CHECK(gestures.swingSettings().raiseMinHeight == SwingSettings{}.raiseMinHeight);
}

TEST_CASE("mapper: the throw presses the equipment launcher and does not punch") {
    MapperSettings settings;
    settings.throwGesture.enabled = true;
    InputMapper mapper(questTouchProfile(), settings);
    mapper.update(restingFrame(), {}, kFrame);
    mapper.update(leftWoundUp(), {}, kFrame);
    const auto input = mapper.update(leftThrowing(3.5f), {}, kFrame);
    CHECK(contains(input.pressed, GameAction::Equipment));
    CHECK_FALSE(contains(input.down, GameAction::Melee));
    CHECK(input.thrown);
    CHECK_FALSE(input.punch[0]);
    // Still moving fast on the next frame: no punch either (the hand has to slow down first).
    CHECK_FALSE(contains(mapper.update(leftThrowing(3.5f), {}, kFrame).down, GameAction::Melee));
    // A punch from the chest still punches.
    mapper.update(restingFrame(), {}, kFrame);
    InputFrame jab = restingFrame();
    jab.left = trackedHand({-0.2f, 1.3f, -0.4f}, {0.0f, 0.0f, -3.5f});
    CHECK(contains(mapper.update(jab, {}, kFrame).pressed, GameAction::Melee));
}

TEST_CASE("mapper: with the throw off, the same motion is a punch") {
    InputMapper mapper(questTouchProfile());
    mapper.update(restingFrame(), {}, kFrame);
    mapper.update(leftWoundUp(), {}, kFrame);
    const auto input = mapper.update(leftThrowing(3.5f), {}, kFrame);
    CHECK(contains(input.pressed, GameAction::Melee));
    CHECK_FALSE(contains(input.down, GameAction::Equipment));
    CHECK_FALSE(input.thrown);
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
