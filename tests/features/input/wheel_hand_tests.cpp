#include "common/quat.hpp"
#include "common/vector.hpp"
#include "features/input/wheel_hand.hpp"
#include "features/input/wheel_mouse.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <ostream>
#include <string_view>

using evr::Quat;
using evr::Vec3;
using evr::input::Axis2;
using evr::input::kDefaultWheelHandDegrees;
using evr::input::WheelDirection;
using evr::input::wheelDirection;
using evr::input::WheelHand;
using evr::input::wheelHandPointer;
using evr::input::WheelSelect;

namespace {

constexpr float kDegrees = std::numbers::pi_v<float> / 180.0f;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
// The wheel's threshold (WheelMouseSettings::selectThreshold).
constexpr float kThreshold = 0.5f;

// An aim orientation: yaw counter-clockwise seen from above (left), then pitch up, then roll about the
// pointing axis, as the tracking space has them (+Y up, -Z forward).
Quat aim(float yawDegrees, float pitchDegrees = 0.0f, float rollDegrees = 0.0f) {
    const Quat yaw = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yawDegrees * kDegrees);
    const Quat pitch = Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, pitchDegrees * kDegrees);
    const Quat roll = Quat::fromAxisAngle({0.0f, 0.0f, -1.0f}, rollDegrees * kDegrees);
    return yaw * pitch * roll;
}

void checkPointer(Axis2 got, Axis2 expected) {
    CHECK(got.x == doctest::Approx(expected.x).epsilon(0.01));
    CHECK(got.y == doctest::Approx(expected.y).epsilon(0.01));
}

} // namespace

TEST_CASE("wheel hand: the turn from the start direction, full at 20 degrees") {
    const Quat start = aim(0.0f);
    checkPointer(wheelHandPointer(start, start), {0.0f, 0.0f});
    // Right is a turn clockwise seen from above (negative yaw), up a positive pitch.
    checkPointer(wheelHandPointer(start, aim(-10.0f)), {0.5f, 0.0f});
    checkPointer(wheelHandPointer(start, aim(-20.0f)), {1.0f, 0.0f});
    checkPointer(wheelHandPointer(start, aim(10.0f)), {-0.5f, 0.0f});
    checkPointer(wheelHandPointer(start, aim(0.0f, 20.0f)), {0.0f, 1.0f});
    checkPointer(wheelHandPointer(start, aim(0.0f, -15.0f)), {0.0f, -0.75f});
    // Past the full turn the pointer stays on the rim.
    checkPointer(wheelHandPointer(start, aim(-60.0f)), {1.0f, 0.0f});
    checkPointer(wheelHandPointer(start, aim(0.0f, 170.0f)), {0.0f, 1.0f});
    // A diagonal turn points diagonally.
    CHECK(wheelDirection(wheelHandPointer(start, aim(-15.0f, 15.0f)), kThreshold) == WheelDirection::UpRight);
    CHECK(wheelDirection(wheelHandPointer(start, aim(15.0f, -15.0f)), kThreshold) ==
          WheelDirection::DownLeft);
}

TEST_CASE("wheel hand: the full turn is a setting, out-of-range values take the default") {
    const Quat start = aim(0.0f);
    checkPointer(wheelHandPointer(start, aim(-10.0f), 10.0f), {1.0f, 0.0f});
    checkPointer(wheelHandPointer(start, aim(-10.0f), 40.0f), {0.25f, 0.0f});
    checkPointer(wheelHandPointer(start, aim(-10.0f), 1.0f), {0.5f, 0.0f});
    checkPointer(wheelHandPointer(start, aim(-10.0f), kNaN), {0.5f, 0.0f});
    CHECK(WheelHand(90.0f).fullDegrees() == kDefaultWheelHandDegrees);
    CHECK(WheelHand(30.0f).fullDegrees() == 30.0f);
}

TEST_CASE("wheel hand: relative to the start pose, whichever way the player faces") {
    for (const float facing : {90.0f, 180.0f, -135.0f}) {
        CAPTURE(facing);
        const Quat start = aim(facing);
        checkPointer(wheelHandPointer(start, aim(facing - 20.0f)), {1.0f, 0.0f});
        checkPointer(wheelHandPointer(start, aim(facing + 10.0f)), {-0.5f, 0.0f});
        checkPointer(wheelHandPointer(start, aim(facing, 20.0f)), {0.0f, 1.0f});
    }
    // A hand held low (pitched down) at the start: up and down are relative to it.
    const Quat low = aim(30.0f, -45.0f);
    checkPointer(wheelHandPointer(low, aim(30.0f, -25.0f)), {0.0f, 1.0f});
    checkPointer(wheelHandPointer(low, aim(30.0f, -55.0f)), {0.0f, -0.5f});
    // Turning a low hand about the room's up still points right.
    CHECK(wheelDirection(wheelHandPointer(low, aim(10.0f, -45.0f)), kThreshold) == WheelDirection::Right);
}

TEST_CASE("wheel hand: roll never moves the pointer") {
    const Quat start = aim(0.0f);
    // Twisting the wrist about the barrel.
    for (const float roll : {-90.0f, -30.0f, 45.0f, 170.0f}) {
        CAPTURE(roll);
        checkPointer(wheelHandPointer(start, aim(0.0f, 0.0f, roll)), {0.0f, 0.0f});
        checkPointer(wheelHandPointer(start, aim(-20.0f, 0.0f, roll)), {1.0f, 0.0f});
        // A roll the hand had at the start does not tilt the frame either.
        checkPointer(wheelHandPointer(aim(0.0f, 0.0f, roll), aim(0.0f, 20.0f)), {0.0f, 1.0f});
        checkPointer(wheelHandPointer(aim(0.0f, 0.0f, roll), aim(-10.0f)), {0.5f, 0.0f});
    }
}

TEST_CASE("wheel hand: straight up or down at the start still gives a frame") {
    for (const float pitch : {90.0f, -90.0f, 89.0f}) {
        CAPTURE(pitch);
        const Quat start = aim(0.0f, pitch);
        const Axis2 away = wheelHandPointer(start, aim(0.0f, pitch - 20.0f));
        CHECK(evr::input::isFinite(away));
        CHECK(evr::input::magnitude(away) == doctest::Approx(1.0f).epsilon(0.01));
        const Axis2 side = wheelHandPointer(start, aim(0.0f, pitch) * aim(-20.0f));
        CHECK(evr::input::isFinite(side));
        CHECK(evr::input::magnitude(side) == doctest::Approx(1.0f).epsilon(0.01));
    }
}

TEST_CASE("wheel hand: orientations that are not usable give no pointer") {
    const Quat start = aim(0.0f);
    CHECK(wheelHandPointer(start, Quat{kNaN, 0.0f, 0.0f, 1.0f}) == Axis2{});
    CHECK(wheelHandPointer(Quat{0.0f, 0.0f, 0.0f, 0.0f}, aim(-20.0f)) == Axis2{});
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(wheelHandPointer(start, Quat{0.0f, inf, 0.0f, 1.0f}) == Axis2{});
    // A quaternion off unit length (a runtime's rounding) is normalised.
    const Quat q = aim(-20.0f);
    checkPointer(wheelHandPointer(start, Quat{q.x * 2.0f, q.y * 2.0f, q.z * 2.0f, q.w * 2.0f}), {1.0f, 0.0f});
}

TEST_CASE("wheel hand: the reference is taken when the wheel is held and the hand tracked") {
    WheelHand hand;
    CHECK(hand.update(false, true, aim(-30.0f)) == Axis2{});
    CHECK_FALSE(hand.anchored());
    // Held with the hand lost: nothing, and no reference yet.
    CHECK(hand.update(true, false, aim(0.0f)) == Axis2{});
    CHECK_FALSE(hand.anchored());
    // The first tracked frame is the reference, whatever it points at.
    CHECK(hand.update(true, true, aim(50.0f)) == Axis2{});
    CHECK(hand.anchored());
    checkPointer(hand.update(true, true, aim(30.0f)), {1.0f, 0.0f});
    checkPointer(hand.update(true, true, aim(45.0f)), {0.25f, 0.0f});
    // Tracking lost for a moment: no pointer (the highlight stays), the reference is kept.
    CHECK(hand.update(true, false, aim(30.0f)) == Axis2{});
    CHECK(hand.update(true, true, Quat{kNaN, kNaN, kNaN, kNaN}) == Axis2{});
    CHECK(hand.anchored());
    checkPointer(hand.update(true, true, aim(50.0f, 20.0f)), {0.0f, 1.0f});
    // Let go: the next hold takes a new reference.
    CHECK(hand.update(false, true, aim(0.0f)) == Axis2{});
    CHECK_FALSE(hand.anchored());
    CHECK(hand.update(true, true, aim(0.0f)) == Axis2{});
    checkPointer(hand.update(true, true, aim(-20.0f)), {1.0f, 0.0f});
}

TEST_CASE("wheel hand: a small turn stays below the wheel's threshold, as a stick near the centre does") {
    WheelHand hand;
    hand.update(true, true, aim(0.0f));
    CHECK(wheelDirection(hand.update(true, true, aim(-6.0f, 3.0f)), kThreshold) == WheelDirection::None);
    CHECK(wheelDirection(hand.update(true, true, aim(-11.0f)), kThreshold) == WheelDirection::Right);
    CHECK(wheelDirection(hand.update(true, true, aim(0.0f, -12.0f)), kThreshold) == WheelDirection::Down);
    // Back near the start: nothing points, so the wheel keeps the last highlight.
    CHECK(wheelDirection(hand.update(true, true, aim(-2.0f)), kThreshold) == WheelDirection::None);
}

TEST_CASE("wheel hand: the pointer drives the wheel's cursor like the stick") {
    evr::input::WheelMouse wheel;
    WheelHand hand;
    constexpr float kFrame = 1.0f / 90.0f;
    // Held with the hand still until the wheel opens: no motion.
    for (int i = 0; i < 60; ++i) {
        const auto out = wheel.update(true, hand.update(true, true, aim(0.0f)), kFrame);
        CHECK_FALSE(out.move);
    }
    REQUIRE(wheel.open());
    const auto out = wheel.update(true, hand.update(true, true, aim(0.0f, 25.0f)), kFrame);
    CHECK(out.move);
    CHECK(out.pointed == WheelDirection::Up);
    CHECK(out.dx == 0);
    CHECK(out.dy == -200); // screen y is down
}

TEST_CASE("wheel select names") {
    CHECK(std::string_view(evr::input::wheelSelectName(WheelSelect::Stick)) == "stick");
    CHECK(std::string_view(evr::input::wheelSelectName(WheelSelect::Hand)) == "hand");
}
