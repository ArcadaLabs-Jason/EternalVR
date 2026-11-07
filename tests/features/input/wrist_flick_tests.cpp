#include "features/input/wrist_flick.hpp"

#include "features/input/input_frames.hpp"
#include "features/input/input_mapper.hpp"
#include "features/input/wrist_flick_rig.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <limits>

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
using evr::test::wrist::flickOn;
using evr::test::wrist::handAt;
using evr::test::wrist::kFrame;
using evr::test::wrist::radians;
using evr::test::wrist::Rig;

namespace {

// The right hand's roll at frame `i` of a flick at 800 degrees per second: 18 frames over (160 degrees), 18
// back.
float overAndBack(int i) {
    return 800.0f * static_cast<float>(i <= 18 ? i : 36 - i) * kFrame;
}

} // namespace

TEST_CASE("the wrist flick (Roll wrist) is on by default, and off it reads nothing") {
    Rig rig{FlickSettings{}};
    CHECK(rig.flick.settings().enabled);
    CHECK(rig.flickOverAndBack() == 1);
    FlickSettings off;
    off.enabled = false;
    Rig quiet{off};
    CHECK_FALSE(quiet.flick.settings().enabled);
    CHECK(quiet.flickOverAndBack() == 0);
}

TEST_CASE("the hand turned over and straight back flicks once, on the way back") {
    Rig rig;
    // Over 160 degrees: nothing yet.
    CHECK(rig.run(800.0f, 0.2f) == 0);
    // Back: the flick.
    CHECK(rig.run(-800.0f, 0.2f) == 1);
    // On round past where it started: no second flick.
    CHECK(rig.run(-800.0f, 0.2f) == 0);
}

TEST_CASE("a roll one way only is no flick, however fast or far (DECISIONS T-202)") {
    Rig rig;
    CHECK(rig.run(1500.0f, 0.3f) == 0);
    CHECK(rig.run(0.0f, 1.0f) == 0);
    CHECK(rig.run(-600.0f, 0.4f) == 0);
    // Turned over and held: the roll back comes too late to make one flick with it.
    Rig held;
    CHECK(held.run(800.0f, 0.2f) == 0);
    CHECK(held.run(0.0f, 0.4f) == 0);
    CHECK(held.run(-1200.0f, 0.15f) == 0);
    // Positive control: the same over with a short pause at the turn (0.15 s) and the same back is a flick.
    Rig paused;
    CHECK(paused.run(800.0f, 0.2f) == 0);
    CHECK(paused.run(0.0f, 0.15f) == 0);
    CHECK(paused.run(-1200.0f, 0.15f) == 1);
}

TEST_CASE("the hand must turn over far enough and come back far enough") {
    // 70 degrees over and back, fast: short of a hand turned over.
    Rig shortOver;
    CHECK(shortOver.flickOverAndBack(1000.0f, 70.0f) == 0);
    // 135 degrees over, then 40 back fast and the rest slowly (40 degrees per second).
    Rig shortBack;
    CHECK(shortBack.run(900.0f, 0.15f) == 0);
    CHECK(shortBack.run(-900.0f, 0.045f) == 0);
    CHECK(shortBack.run(-40.0f, 1.5f) == 0);
    // Positive control: 100 over and 100 back at the same rate.
    Rig enough;
    CHECK(enough.flickOverAndBack(1000.0f, 100.0f) == 1);
}

TEST_CASE(
    "a flick carries its sizes and rates for the log: over, back, pointing pitch, hand speed, head turn") {
    WristFlick flick(flickOn());
    float roll = 0.0f;
    float headYaw = 0.0f;
    FlickOutput fired;
    for (int i = 0; i < 30 && !fired.flicked; ++i) {
        roll += (i < 15 ? 900.0f : -900.0f) * kFrame; // 150 degrees over, then back
        headYaw += 120.0f * kFrame;                   // the player turning left
        InputFrame frame = handAt(roll, 0.0f, -30.0f, {0.3f, 0.0f, 0.4f});
        frame.head.poseValid = true;
        frame.head.pose.orientation = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, radians(headYaw));
        const FlickOutput out = flick.update(frame, Hand::Right, kFrame);
        if (out.flicked) {
            fired = out;
        }
    }
    REQUIRE(fired.flicked);
    // The first frame reads no rate: over from 10 degrees to 150.
    CHECK(fired.overDegrees == doctest::Approx(140.0f).epsilon(0.02));
    CHECK(fired.backDegrees >= 60.0f);
    CHECK(fired.backDegrees < 90.0f);
    CHECK(fired.pitchDegrees == doctest::Approx(-30.0f).epsilon(0.01));
    CHECK(fired.handSpeed == doctest::Approx(0.5f));
    CHECK(fired.headTurnDegrees == doctest::Approx(120.0f).epsilon(0.01));
}

TEST_CASE("the roll is read in either direction") {
    Rig rig;
    CHECK(rig.flickOverAndBack(-800.0f) == 1);
}

TEST_CASE("a flick has to be fast: a slow turn of the wrist is not one") {
    Rig rig;
    CHECK(rig.flickOverAndBack(150.0f) == 0);
    CHECK(rig.flickOverAndBack(300.0f) == 0);
    CHECK(rig.flickOverAndBack(400.0f) == 0);
    // Positive control: past the minimum rate the same roll flicks.
    CHECK(rig.flickOverAndBack(700.0f) == 1);
}

TEST_CASE("a fast turn of the pointing direction is aiming, not a flick") {
    Rig rig;
    // Swinging the aim sideways, level and tilted up: no roll, or a roll that is less than twice the swing of
    // the pointing direction.
    CHECK(rig.run(0.0f, 0.4f, 900.0f, 0.0f) == 0);
    CHECK(rig.run(0.0f, 0.4f, 900.0f, 45.0f) == 0);
    CHECK(rig.run(0.0f, 0.4f, 700.0f, 60.0f) == 0);
    CHECK(rig.run(0.0f, 0.4f, -900.0f, 0.0f) == 0);
    // Positive control: the same rotation rate about the pointing axis, over and back, is a flick.
    Rig roll;
    CHECK(roll.flickOverAndBack(900.0f) == 1);
}

TEST_CASE("a roll while the aim also swings fast is not a flick") {
    Rig rig;
    CHECK(rig.run(700.0f, 0.2f, 600.0f) == 0);
    CHECK(rig.run(-700.0f, 0.2f, -600.0f) == 0);
    // Positive control: the same roll with the aim still.
    Rig still;
    CHECK(still.run(700.0f, 0.2f) == 0);
    CHECK(still.run(-700.0f, 0.2f) == 1);
}

TEST_CASE("a roll while the hand travels fast is not a flick") {
    Rig rig;
    // The over with the hand travelling at 3 m/s, the back with it still.
    CHECK(rig.run(800.0f, 0.2f, 0.0f, 0.0f, Vec3{0.0f, 0.0f, -3.0f}) == 0);
    CHECK(rig.run(-800.0f, 0.2f) == 0);
    Rig still;
    CHECK(still.run(800.0f, 0.2f) == 0);
    CHECK(still.run(-800.0f, 0.2f) == 1);
}

TEST_CASE("a tracking jump is no flick") {
    Rig rig;
    // 150 degrees in one frame is 13500 degrees per second: no wrist does that.
    CHECK_FALSE(rig.flick.update(handAt(150.0f), Hand::Right, kFrame).flicked);
    CHECK_FALSE(rig.flick.update(handAt(150.0f), Hand::Right, kFrame).flicked);
    // The hand is read again from where it is now.
    rig.roll = 150.0f;
    CHECK(rig.flickOverAndBack(800.0f) == 1);
}

TEST_CASE("the second flick has to wait for the cooldown and for the roll to slow down") {
    Rig rig;
    CHECK(rig.flickOverAndBack(1200.0f) == 1);
    // At once over and back again, and again: within the cooldown (0.6 s), no new flick.
    CHECK(rig.flickOverAndBack(1200.0f) == 0);
    CHECK(rig.flickOverAndBack(1200.0f) == 0);
    // Still, for longer than the cooldown, then another flick.
    CHECK(rig.run(0.0f, 0.8f) == 0);
    CHECK(rig.flickOverAndBack(1200.0f) == 1);
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

TEST_CASE("one flick and back is one press; a second needs the hand turned over and back again") {
    // Over, back, and on round past where it started: one flick.
    Rig rig;
    CHECK(rig.run(800.0f, 0.2f) == 0);
    CHECK(rig.run(-800.0f, 0.4f) == 1);
    // A second flick after a still moment, the other way round.
    CHECK(rig.run(0.0f, 0.8f) == 0);
    CHECK(rig.flickOverAndBack(800.0f) == 1);
    // A roll back to where the hand started, after a still moment, is no flick of its own.
    Rig back;
    CHECK(back.flickOverAndBack(800.0f) == 1);
    CHECK(back.run(0.0f, 1.0f) == 0);
    CHECK(back.run(800.0f, 0.15f) == 0);
}

TEST_CASE("the rate comes from the poses' sample times, not from how often the mapper runs") {
    // The mapper runs twice per 90 Hz sample, the new sample 2 ms after the last read: from the mapper's time
    // a 400 deg/s roll reads in spikes that add up past the minimum (450). The hand turns over for 25 samples
    // and back for 25.
    const auto rolled = [](float rate, bool sampleTimes) {
        WristFlick flick(flickOn());
        int count = 0;
        for (int i = 0; i < 50; ++i) {
            InputFrame frame = handAt(rate * static_cast<float>(i <= 25 ? i : 50 - i) * kFrame);
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
        for (int i = 1; i <= 36; ++i) {
            InputFrame frame = handAt(90.0f + overAndBack(i));
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
    // Reason 3 holds nothing back: the positive control.
    for (const int reason : {0, 1, 2, 3}) {
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
        int presses = 0;
        for (int i = 1; i <= 36; ++i) {
            InputFrame frame = handAt(overAndBack(i));
            frame.right.stick = start.right.stick;
            const auto input = mapper.update(frame, context, kFrame);
            presses += input.flicked || contains(input.pressed, GameAction::SwitchWeaponMod) ? 1 : 0;
        }
        CHECK(presses == (reason == 3 ? 1 : 0));
    }
}

TEST_CASE("only the weapon hand's roll flicks") {
    for (const Hand weaponHand : {Hand::Right, Hand::Left}) {
        WristFlick flick(flickOn());
        int count = 0;
        for (int i = 0; i <= 36; ++i) {
            InputFrame frame = restingFrame();
            // The left hand turns over and back; the right hand is still.
            frame.left.aimPose.orientation =
                Quat::fromAxisAngle({0.0f, 0.0f, -1.0f}, radians(overAndBack(i)));
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
    for (int i = 1; i <= 36; ++i) {
        const auto input = mapper.update(handAt(overAndBack(i)), {}, kFrame);
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
    for (int i = 1; i <= 36; ++i) {
        const auto input = mapper.update(handAt(overAndBack(i)), {}, kFrame);
        presses += contains(input.pressed, GameAction::SwitchWeaponMod) ? 1 : 0;
    }
    CHECK(presses == 1);
}

TEST_CASE("mapper: with the flick off the same roll presses nothing") {
    MapperSettings settings;
    settings.flick.enabled = false;
    InputMapper mapper(questTouchProfile(), settings);
    mapper.update(handAt(0.0f), {}, kFrame);
    for (int i = 1; i <= 36; ++i) {
        const auto input = mapper.update(handAt(overAndBack(i)), {}, kFrame);
        CHECK_FALSE(contains(input.down, GameAction::SwitchWeaponMod));
        CHECK_FALSE(input.flicked);
    }
}
