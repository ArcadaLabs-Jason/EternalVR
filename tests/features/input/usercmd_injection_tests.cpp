#include "features/input/usercmd_injection.hpp"

#include "features/input/turn_policy.hpp"
#include "features/input/usercmd_motion.hpp"
#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <ostream>

using evr::game::add;
using evr::game::contains;
using evr::game::GameAction;
using evr::game::GameActionSet;
using evr::input::ActionHold;
using evr::input::addAccumulatedYaw;
using evr::input::addMoveAxis;
using evr::input::Axis2;
using evr::input::mergeButtons;
using evr::input::quantizeMove;
using evr::input::TurnMode;
using evr::input::TurnPolicy;
using evr::input::TurnSettings;
using evr::input::ViewDelta;
using evr::input::ViewDeltaQueue;
using evr::test::approxEqual;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

GameActionSet only(GameAction action) {
    GameActionSet set;
    add(set, action);
    return set;
}

// The command's 16-bit yaw for an accumulated angle, as the game converts it (0x17FD3C0).
std::int16_t yawShort(float degrees) {
    const auto units = static_cast<std::int32_t>(degrees * 182.04445f);
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(units & 0xFFFF));
}

// The game truncates toward zero, so the same heading reached from either side of zero can differ by
// one unit (1/182 of a degree).
bool sameHeading(std::int16_t a, std::int16_t b) {
    const auto d = static_cast<std::int16_t>(static_cast<std::uint16_t>(a - b));
    return d >= -1 && d <= 1;
}

} // namespace

TEST_CASE("real buttons are kept and ours are ORed in") {
    CHECK(mergeButtons(0x1, 0x100000000, false) == 0x100000001);
    CHECK(mergeButtons(0x0, 0x0, false) == 0x0);
    CHECK(mergeButtons(0x40, 0x40, false) == 0x40);
}

TEST_CASE("nothing is ORed in while the game suppresses buttons") {
    CHECK(mergeButtons(0x0, 0x1, true) == 0x0);
    CHECK(mergeButtons(0x8, 0x1, true) == 0x8);
}

TEST_CASE("movement adds to the keys and clamps to the key range") {
    CHECK(addMoveAxis(0, 64) == 64);
    CHECK(addMoveAxis(127, 64) == 127);
    CHECK(addMoveAxis(-127, -64) == -127);
    CHECK(addMoveAxis(127, -127) == 0);
    CHECK(addMoveAxis(0, 1000) == 127);
    CHECK(addMoveAxis(0, -1000) == -127);
    // With nothing to add, the game's own value is left exactly as it was.
    CHECK(addMoveAxis(-128, 0) == -128);
    CHECK(addMoveAxis(-128, -5) == -127);
}

TEST_CASE("a stick move reaches the command as the keys would give it") {
    const auto forward = quantizeMove(Axis2{0.0f, 1.0f}, evr::input::kMaxMoveAxis);
    CHECK(addMoveAxis(0, forward.forward) == 127);
    CHECK(addMoveAxis(0, forward.right) == 0);
    const auto diagonal = quantizeMove(Axis2{1.0f, 1.0f}, evr::input::kMaxMoveAxis);
    CHECK(diagonal.forward == 90);
    CHECK(diagonal.right == 90);
}

TEST_CASE("a one-frame tap is held for the minimum time and number of commands") {
    ActionHold hold(0.05f, 2);
    CHECK(contains(hold.update(only(GameAction::QuickSwitch), kFrame), GameAction::QuickSwitch));
    int frames = 1;
    while (contains(hold.update({}, kFrame), GameAction::QuickSwitch)) {
        ++frames;
        REQUIRE(frames < 100);
    }
    // 0.05 s at 90 Hz: the tap frame plus the frames until 0.05 s have passed.
    CHECK(frames == 5);
}

TEST_CASE("a tap across a long hitch still lasts two commands") {
    ActionHold hold(0.05f, 2);
    CHECK(contains(hold.update(only(GameAction::Jump), 0.2f), GameAction::Jump));
    CHECK(contains(hold.update({}, 0.2f), GameAction::Jump));
    CHECK_FALSE(contains(hold.update({}, 0.2f), GameAction::Jump));
}

TEST_CASE("an action held longer than the minimum is released at once") {
    ActionHold hold(0.05f, 2);
    for (int i = 0; i < 20; ++i) {
        CHECK(contains(hold.update(only(GameAction::Fire), kFrame), GameAction::Fire));
    }
    CHECK_FALSE(contains(hold.update({}, kFrame), GameAction::Fire));
}

TEST_CASE("each action is held on its own clock") {
    ActionHold hold(0.05f, 2);
    GameActionSet both = only(GameAction::Fire);
    add(both, GameAction::Dash);
    for (int i = 0; i < 10; ++i) {
        hold.update(only(GameAction::Fire), kFrame);
    }
    const GameActionSet out = hold.update(both, kFrame);
    CHECK(contains(out, GameAction::Dash));
    const GameActionSet next = hold.update({}, kFrame);
    CHECK_FALSE(contains(next, GameAction::Fire));
    CHECK(contains(next, GameAction::Dash));
}

TEST_CASE("non-finite or negative frame times count as zero") {
    ActionHold hold(0.05f, 2);
    hold.update(only(GameAction::Dash), std::numeric_limits<float>::quiet_NaN());
    CHECK(contains(hold.update({}, -1.0f), GameAction::Dash));
    CHECK(contains(hold.update({}, std::numeric_limits<float>::infinity()), GameAction::Dash));
    CHECK_FALSE(contains(hold.update({}, 0.06f), GameAction::Dash));
}

TEST_CASE("view deltas add up until drained, and drain once") {
    ViewDeltaQueue queue;
    queue.add({1.0f, 45.0f});
    queue.add({-0.5f, 45.0f});
    queue.add({std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()});
    const ViewDelta out = queue.drain();
    CHECK(out.pitch == doctest::Approx(0.5f));
    CHECK(out.yaw == doctest::Approx(90.0f));
    CHECK(queue.drain().yaw == 0.0f);
}

TEST_CASE("snap turns through the queue and the accumulator make a whole circle both ways") {
    for (const float direction : {-1.0f, 1.0f}) {
        CAPTURE(direction);
        TurnSettings settings;
        settings.mode = TurnMode::Snap;
        settings.snapDegrees = 45.0f;
        TurnPolicy turn(settings);
        ViewDeltaQueue queue;
        float accumulated = 12.5f;
        const std::int16_t start = yawShort(accumulated);
        int snaps = 0;
        for (int flick = 0; flick < 8; ++flick) {
            // Flick the stick and let it return: one snap per flick.
            for (int i = 0; i < 5; ++i) {
                const float d = turn.update(Axis2{direction, 0.0f}, kFrame, true);
                snaps += d != 0.0f ? 1 : 0;
                queue.add({0.0f, d});
            }
            for (int i = 0; i < 5; ++i) {
                queue.add({0.0f, turn.update(Axis2{}, kFrame, true)});
            }
            accumulated = addAccumulatedYaw(accumulated, queue.drain().yaw);
        }
        CHECK(snaps == 8);
        CHECK(approxEqual(accumulated, 12.5f - direction * 360.0f, 1e-3f));
        CHECK(sameHeading(yawShort(accumulated), start));
    }
}

TEST_CASE("a smooth turn at the set rate completes 360 degrees in the expected time") {
    TurnSettings settings;
    settings.mode = TurnMode::Smooth;
    settings.smoothDegreesPerSecond = 240.0f;
    TurnPolicy turn(settings);
    ViewDeltaQueue queue;
    float accumulated = 0.0f;
    float seconds = 0.0f;
    while (std::fabs(accumulated) < 360.0f && seconds < 10.0f) {
        queue.add({0.0f, turn.update(Axis2{1.0f, 0.0f}, kFrame, true)});
        accumulated = addAccumulatedYaw(accumulated, queue.drain().yaw);
        seconds += kFrame;
    }
    CHECK(accumulated < 0.0f); // stick right turns clockwise: negative id Tech yaw
    CHECK(seconds == doctest::Approx(1.5f).epsilon(0.02));
}

TEST_CASE("the accumulated yaw stays bounded and keeps its command angle when wrapped") {
    float yaw = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        yaw = addAccumulatedYaw(yaw, 45.0f);
        CHECK(std::fabs(yaw) <= 3600.0f + 45.0f);
    }
    // 1000 snaps of 45 degrees are 125 whole turns: back where it started.
    CHECK(sameHeading(yawShort(yaw), yawShort(0.0f)));
    CHECK(addAccumulatedYaw(10.0f, std::numeric_limits<float>::quiet_NaN()) == 10.0f);
}
