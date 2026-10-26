#include "features/input/wrist_flick.hpp"

#include "features/input/input_frames.hpp"
#include "features/input/input_mapper.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>

using evr::Quat;
using evr::Vec3;
using evr::game::contains;
using evr::game::GameAction;
using evr::input::FlickOutput;
using evr::input::FlickSettings;
using evr::input::Hand;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::MapperContext;
using evr::input::MapperSettings;
using evr::input::WristFlick;
using evr::test::questTouchProfile;
using evr::test::restingFrame;

namespace {

constexpr float kFrame = 1.0f / 90.0f;
constexpr float kPi = std::numbers::pi_v<float>;

float radians(float degrees) {
    return degrees * kPi / 180.0f;
}

FlickSettings flickOn() {
    FlickSettings s;
    s.enabled = true;
    return s;
}

// The right hand pitched up by `pitchDegrees`, rolled by `rollDegrees` about its pointing axis and turned by
// `yawDegrees` about the vertical, moving at `velocity`.
InputFrame handAt(float rollDegrees, float yawDegrees = 0.0f, float pitchDegrees = 0.0f, Vec3 velocity = {}) {
    InputFrame frame = restingFrame();
    const Quat pitch = Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, radians(pitchDegrees));
    const Quat roll = Quat::fromAxisAngle({0.0f, 0.0f, -1.0f}, radians(rollDegrees));
    const Quat yaw = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, radians(yawDegrees));
    frame.right.aimPose.orientation = yaw * pitch * roll;
    frame.right.linearVelocity = velocity;
    return frame;
}

// A weapon hand moving continuously: its roll and yaw carry on from where the last run ended.
struct Rig {
    explicit Rig(FlickSettings settings = flickOn()) : flick(settings) { step(0.0f, 0.0f); }

    FlickOutput step(float rollRate, float yawRate, float pitch = 0.0f, Vec3 velocity = {}) {
        roll += rollRate * kFrame;
        yaw += yawRate * kFrame;
        return flick.update(handAt(roll, yaw, pitch, velocity), Hand::Right, kFrame);
    }

    // `seconds` of a constant roll rate and yaw rate (degrees per second); how many frames flicked.
    int run(float rollRate, float seconds, float yawRate = 0.0f, float pitch = 0.0f, Vec3 velocity = {}) {
        int count = 0;
        const int frames = static_cast<int>(seconds / kFrame);
        for (int i = 0; i < frames; ++i) {
            count += step(rollRate, yawRate, pitch, velocity).flicked ? 1 : 0;
        }
        return count;
    }

    WristFlick flick;
    float roll = 0.0f;
    float yaw = 0.0f;
};

} // namespace

TEST_CASE("the wrist flick (Roll wrist) is on by default, and off it reads nothing") {
    Rig rig{FlickSettings{}};
    CHECK(rig.flick.settings().enabled);
    CHECK(rig.run(900.0f, 0.3f) == 1);
    FlickSettings off;
    off.enabled = false;
    Rig quiet{off};
    CHECK_FALSE(quiet.flick.settings().enabled);
    CHECK(quiet.run(900.0f, 0.3f) == 0);
}

TEST_CASE("a fast roll about the pointing axis flicks once, and the roll back does not flick again") {
    Rig rig;
    CHECK(rig.run(800.0f, 0.2f) == 1);
    // Rolls back at once: still the one flick.
    CHECK(rig.run(-800.0f, 0.2f) == 0);
}

TEST_CASE("a flick carries its rates for the log: pointing pitch, hand speed and the head's turn") {
    WristFlick flick(flickOn());
    float roll = 0.0f;
    float headYaw = 0.0f;
    FlickOutput fired;
    for (int i = 0; i < 30 && !fired.flicked; ++i) {
        roll += 900.0f * kFrame;
        headYaw += 120.0f * kFrame; // the player turning left
        InputFrame frame = handAt(roll, 0.0f, -30.0f, {0.3f, 0.0f, 0.4f});
        frame.head.poseValid = true;
        frame.head.pose.orientation = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, radians(headYaw));
        const FlickOutput out = flick.update(frame, Hand::Right, kFrame);
        if (out.flicked) {
            fired = out;
        }
    }
    REQUIRE(fired.flicked);
    CHECK(fired.pitchDegrees == doctest::Approx(-30.0f).epsilon(0.01));
    CHECK(fired.handSpeed == doctest::Approx(0.5f));
    CHECK(fired.headTurnDegrees == doctest::Approx(120.0f).epsilon(0.01));
}

TEST_CASE("the roll is read in either direction") {
    Rig rig;
    CHECK(rig.run(-800.0f, 0.2f) == 1);
}

TEST_CASE("a flick has to be fast: a slow turn of the wrist is not one") {
    Rig rig;
    CHECK(rig.run(150.0f, 1.0f) == 0);
    CHECK(rig.run(300.0f, 1.0f) == 0);
    CHECK(rig.run(400.0f, 1.0f) == 0);
    // Positive control: past the minimum rate the same roll flicks.
    CHECK(rig.run(700.0f, 0.2f) == 1);
}

TEST_CASE("a fast turn of the pointing direction is aiming, not a flick") {
    Rig rig;
    // Swinging the aim sideways, level and tilted up: no roll, or a roll that is less than twice the swing of
    // the pointing direction.
    CHECK(rig.run(0.0f, 0.4f, 900.0f, 0.0f) == 0);
    CHECK(rig.run(0.0f, 0.4f, 900.0f, 45.0f) == 0);
    CHECK(rig.run(0.0f, 0.4f, 700.0f, 60.0f) == 0);
    // Positive control: the same rotation rate about the pointing axis is a flick.
    Rig roll;
    CHECK(roll.run(900.0f, 0.4f) == 1);
}

TEST_CASE("a roll while the aim also swings fast is not a flick") {
    Rig rig;
    CHECK(rig.run(700.0f, 0.3f, 600.0f) == 0);
    // Positive control: the same roll with the aim still.
    Rig still;
    CHECK(still.run(700.0f, 0.3f) == 1);
}

TEST_CASE("a roll while the hand travels fast is not a flick") {
    Rig rig;
    CHECK(rig.run(800.0f, 0.2f, 0.0f, 0.0f, Vec3{0.0f, 0.0f, -3.0f}) == 0);
    Rig still;
    CHECK(still.run(800.0f, 0.2f) == 1);
}

TEST_CASE("a tracking jump is no flick") {
    Rig rig;
    // 150 degrees in one frame is 13500 degrees per second: no wrist does that.
    CHECK_FALSE(rig.flick.update(handAt(150.0f), Hand::Right, kFrame).flicked);
    CHECK_FALSE(rig.flick.update(handAt(150.0f), Hand::Right, kFrame).flicked);
    // The hand is read again from where it is now.
    rig.roll = 150.0f;
    CHECK(rig.run(800.0f, 0.2f) == 1);
}

TEST_CASE("the second flick has to wait for the cooldown and for the roll to slow down") {
    Rig rig;
    CHECK(rig.run(800.0f, 0.2f) == 1);
    // Rolling straight on is no new flick, and neither is a faster roll the other way within the cooldown.
    CHECK(rig.run(800.0f, 0.2f) == 0);
    CHECK(rig.run(-800.0f, 0.2f) == 0);
    // Still, for longer than the cooldown (0.6 s), then another flick.
    CHECK(rig.run(0.0f, 0.8f) == 0);
    CHECK(rig.run(800.0f, 0.2f) == 1);
}

TEST_CASE("a hand that is not tracked starts over, and a stalled frame reads no rate") {
    Rig rig;
    InputFrame lost = handAt(0.0f);
    lost.right.poseValid = false;
    rig.flick.update(lost, Hand::Right, kFrame);
    // The first tracked frame after the loss is a different orientation: no rate from it.
    CHECK_FALSE(rig.flick.update(handAt(90.0f), Hand::Right, kFrame).flicked);
    // A 0.5 s stall between two frames 180 degrees apart.
    Rig stalled;
    CHECK_FALSE(stalled.flick.update(handAt(180.0f), Hand::Right, 0.5f).flicked);
}

TEST_CASE("the roll back of a flick does not fire; held still a while, it is a flick of its own") {
    // Rolled out fast, then on slowly (never quiet), then back fast after the cooldown: still the one flick.
    Rig rig;
    CHECK(rig.run(800.0f, 0.2f) == 1);
    CHECK(rig.run(200.0f, 0.6f) == 0);
    CHECK(rig.run(-1200.0f, 0.15f) == 0);
    // Turned over, held a moment (0.35 s, past the cooldown with the roll back), turned back: one flick.
    Rig paused;
    CHECK(paused.run(800.0f, 0.2f) == 1);
    CHECK(paused.run(0.0f, 0.35f) == 0);
    CHECK(paused.run(-1200.0f, 0.3f) == 0);
    // Held still for a second, then back fast: a second flick (the weapon mod switched back).
    Rig held;
    CHECK(held.run(800.0f, 0.2f) == 1);
    CHECK(held.run(0.0f, 1.0f) == 0);
    CHECK(held.run(-1200.0f, 0.15f) == 1);
    // A second flick the same way needs only the short quiet (0.2 s) and the cooldown.
    Rig again;
    CHECK(again.run(800.0f, 0.2f) == 1);
    CHECK(again.run(0.0f, 0.5f) == 0);
    CHECK(again.run(1200.0f, 0.15f) == 1);
}

TEST_CASE("the rate comes from the poses' sample times, not from how often the mapper runs") {
    // The mapper runs twice per 90 Hz sample, the new sample 2 ms after the last read: from the mapper's time
    // a 400 deg/s roll reads in spikes that add up past the minimum (450).
    const auto rolled = [](float rate, bool sampleTimes) {
        WristFlick flick(flickOn());
        int count = 0;
        for (int i = 0; i < 40; ++i) {
            InputFrame frame = handAt(rate * static_cast<float>(i) * kFrame);
            frame.sampleSeconds = sampleTimes ? 1.0 + static_cast<double>(i) * kFrame : 0.0;
            count += flick.update(frame, Hand::Right, 0.002f).flicked ? 1 : 0;
            count += flick.update(frame, Hand::Right, kFrame - 0.002f).flicked ? 1 : 0;
        }
        return count;
    };
    CHECK(rolled(400.0f, true) == 0);
    CHECK(rolled(700.0f, true) == 1); // positive control
    // Without sample times the same reads give the overestimate the sample times avoid.
    CHECK(rolled(400.0f, false) == 1);
}

TEST_CASE("mapper: a stall (a load, five seconds) between two frames is no flick") {
    MapperSettings settings;
    settings.flick.enabled = true;
    for (const bool sampleTimes : {false, true}) {
        CAPTURE(sampleTimes);
        InputMapper mapper(questTouchProfile(), settings);
        InputFrame before = handAt(0.0f);
        before.sampleSeconds = sampleTimes ? 10.0 : 0.0;
        mapper.update(before, {}, kFrame);
        // The wrist turned 90 degrees across the stall: the mapper caps its frame time at 0.1 s.
        InputFrame after = handAt(90.0f);
        after.sampleSeconds = sampleTimes ? 15.0 : 0.0;
        CHECK_FALSE(mapper.update(after, {}, 5.0f).flicked);
        for (int i = 1; i <= 30; ++i) {
            InputFrame still = handAt(90.0f);
            still.sampleSeconds = sampleTimes ? 15.0 + i * static_cast<double>(kFrame) : 0.0;
            CHECK_FALSE(mapper.update(still, {}, kFrame).flicked);
        }
        // Positive control: a real flick afterwards.
        int presses = 0;
        for (int i = 1; i <= 18; ++i) {
            InputFrame frame = handAt(90.0f + 800.0f * static_cast<float>(i) * kFrame);
            frame.sampleSeconds = sampleTimes ? 16.0 + i * static_cast<double>(kFrame) : 0.0;
            presses +=
                contains(mapper.update(frame, {}, kFrame).pressed, GameAction::SwitchWeaponMod) ? 1 : 0;
        }
        CHECK(presses == 1);
    }
}

TEST_CASE("mapper: no flick while the game holds the gestures back or the weapon wheel is up") {
    MapperSettings settings;
    settings.flick.enabled = true;
    for (const int reason : {0, 1, 2}) {
        CAPTURE(reason);
        InputMapper mapper(questTouchProfile(), settings);
        MapperContext context;
        context.restWheelBlocked = reason == 0;
        context.menuHold = reason == 1;
        InputFrame start = handAt(0.0f);
        if (reason == 2) {
            start.right.stick = {0.0f, -1.0f}; // the turn stick's down hold opens the weapon wheel
            for (int i = 0; i < 40; ++i) {
                mapper.update(start, context, kFrame);
            }
        }
        mapper.update(start, context, kFrame);
        for (int i = 1; i <= 18; ++i) {
            InputFrame frame = handAt(800.0f * static_cast<float>(i) * kFrame);
            frame.right.stick = start.right.stick;
            const auto input = mapper.update(frame, context, kFrame);
            CHECK_FALSE(input.flicked);
            CHECK_FALSE(contains(input.pressed, GameAction::SwitchWeaponMod));
        }
    }
}

TEST_CASE("only the weapon hand's roll flicks") {
    for (const Hand weaponHand : {Hand::Right, Hand::Left}) {
        WristFlick flick(flickOn());
        int count = 0;
        for (int i = 0; i <= 18; ++i) {
            InputFrame frame = restingFrame();
            // The left hand rolls fast; the right hand is still.
            frame.left.aimPose.orientation =
                Quat::fromAxisAngle({0.0f, 0.0f, -1.0f}, radians(800.0f * static_cast<float>(i) * kFrame));
            count += flick.update(frame, weaponHand, kFrame).flicked ? 1 : 0;
        }
        // Only the left hand's roll counts, and only for a left-handed player.
        CHECK(count == (weaponHand == Hand::Left ? 1 : 0));
    }
}

TEST_CASE("tuning values that are unusable fall back to the defaults") {
    FlickSettings bad = flickOn();
    bad.minRollDegrees = std::numeric_limits<float>::quiet_NaN();
    bad.dominance = 0.1f;
    bad.maxHandSpeed = -1.0f;
    bad.cooldownSeconds = 99.0f;
    const FlickSettings fixed = WristFlick(bad).settings();
    const FlickSettings defaults;
    CHECK(fixed.enabled);
    CHECK(fixed.minRollDegrees == defaults.minRollDegrees);
    CHECK(fixed.dominance == defaults.dominance);
    CHECK(fixed.maxHandSpeed == defaults.maxHandSpeed);
    CHECK(fixed.cooldownSeconds == defaults.cooldownSeconds);
    FlickSettings low = flickOn();
    low.minRollDegrees = 50.0f; // below the range: the default, not a hair trigger
    CHECK(WristFlick(low).settings().minRollDegrees == defaults.minRollDegrees);
}

TEST_CASE("mapper: a flick presses the weapon mod switch once") {
    MapperSettings settings;
    settings.flick.enabled = true;
    InputMapper mapper(questTouchProfile(), settings);
    mapper.update(handAt(0.0f), {}, kFrame);
    int presses = 0;
    for (int i = 1; i <= 18; ++i) {
        const auto input = mapper.update(handAt(800.0f * static_cast<float>(i) * kFrame), {}, kFrame);
        if (contains(input.pressed, GameAction::SwitchWeaponMod)) {
            ++presses;
            CHECK(input.flicked);
        }
    }
    CHECK(presses == 1);
}

TEST_CASE("mapper: the flick is on with the default settings") {
    InputMapper mapper(questTouchProfile());
    mapper.update(handAt(0.0f), {}, kFrame);
    int presses = 0;
    for (int i = 1; i <= 18; ++i) {
        const auto input = mapper.update(handAt(800.0f * static_cast<float>(i) * kFrame), {}, kFrame);
        presses += contains(input.pressed, GameAction::SwitchWeaponMod) ? 1 : 0;
    }
    CHECK(presses == 1);
}

TEST_CASE("mapper: with the flick off the same roll presses nothing") {
    MapperSettings settings;
    settings.flick.enabled = false;
    InputMapper mapper(questTouchProfile(), settings);
    mapper.update(handAt(0.0f), {}, kFrame);
    for (int i = 1; i <= 18; ++i) {
        const auto input = mapper.update(handAt(800.0f * static_cast<float>(i) * kFrame), {}, kFrame);
        CHECK_FALSE(contains(input.down, GameAction::SwitchWeaponMod));
        CHECK_FALSE(input.flicked);
    }
}
