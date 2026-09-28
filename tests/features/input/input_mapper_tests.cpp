#include "features/input/input_mapper.hpp"

#include "features/input/binding_overrides.hpp"
#include "features/input/input_frames.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <ostream>
#include <vector>

using evr::game::contains;
using evr::game::GameAction;
using evr::game::GameActionSet;
using evr::game::Handedness;
using evr::input::Axis2;
using evr::input::BindingProfile;
using evr::input::ButtonBinding;
using evr::input::ButtonInput;
using evr::input::GameInput;
using evr::input::Hand;
using evr::input::HandState;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::MapperContext;
using evr::input::MapperSettings;
using evr::input::PressKind;
using evr::input::resolveBindings;
using evr::input::TurnMode;
using evr::test::questTouchBindings;
using evr::test::questTouchProfile;
using evr::test::restingFrame;
using evr::test::trackedHand;
using evr::test::yawPose;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

HandState& handOf(InputFrame& frame, Hand hand) {
    return hand == Hand::Left ? frame.left : frame.right;
}

void press(InputFrame& frame, Hand hand, ButtonInput input) {
    HandState& state = handOf(frame, hand);
    switch (input) {
    case ButtonInput::Trigger:
        state.trigger = 1.0f;
        break;
    case ButtonInput::Grip:
        state.grip = 1.0f;
        break;
    case ButtonInput::StickClick:
        state.stickClick = true;
        break;
    case ButtonInput::Primary:
        state.primaryButton = true;
        break;
    case ButtonInput::Secondary:
        state.secondaryButton = true;
        break;
    case ButtonInput::Menu:
        state.menuButton = true;
        break;
    case ButtonInput::Count:
        break;
    }
}

Hand buttonSwapHand(Hand hand, ButtonInput input) {
    const bool swaps =
        input == ButtonInput::Trigger || input == ButtonInput::Grip || input == ButtonInput::StickClick;
    return swaps ? evr::input::otherHand(hand) : hand;
}

Hand fullMirrorHand(Hand hand, ButtonInput input) {
    return input == ButtonInput::Menu ? hand : evr::input::otherHand(hand);
}

// Presses one input for a single frame on a fresh mapper and returns the actions that went down.
GameInput pressOnce(const BindingProfile& profile, Hand hand, ButtonInput input) {
    InputMapper mapper(profile);
    mapper.update(restingFrame(), {}, kFrame);
    InputFrame frame = restingFrame();
    press(frame, hand, input);
    return mapper.update(frame, {}, kFrame);
}

MapperSettings snapTurn() {
    MapperSettings settings;
    settings.turn.mode = TurnMode::Snap;
    return settings;
}

InputFrame withStick(Hand hand, Axis2 stick) {
    InputFrame frame = restingFrame();
    handOf(frame, hand).stick = stick;
    return frame;
}

} // namespace

TEST_CASE("an idle frame produces nothing") {
    InputMapper mapper(questTouchProfile());
    const GameInput input = mapper.update(restingFrame(), {}, kFrame);
    CHECK(input.down.none());
    CHECK(input.move == Axis2{});
    CHECK(input.turnDegrees == 0.0f);
}

TEST_CASE("held buttons produce actions with press and release edges") {
    InputMapper mapper(questTouchProfile());
    InputFrame firing = restingFrame();
    firing.right.trigger = 0.9f;

    const GameInput first = mapper.update(firing, {}, kFrame);
    CHECK(contains(first.down, GameAction::Fire));
    CHECK(contains(first.pressed, GameAction::Fire));

    const GameInput held = mapper.update(firing, {}, kFrame);
    CHECK(contains(held.down, GameAction::Fire));
    CHECK_FALSE(contains(held.pressed, GameAction::Fire));

    const GameInput released = mapper.update(restingFrame(), {}, kFrame);
    CHECK_FALSE(contains(released.down, GameAction::Fire));
    CHECK(contains(released.released, GameAction::Fire));
}

TEST_CASE("trigger hysteresis gives one fire press per squeeze") {
    InputMapper mapper(questTouchProfile());
    int presses = 0;
    for (const float value : {0.2f, 0.6f, 0.5f, 0.56f, 0.4f, 0.54f, 0.9f, 0.1f}) {
        InputFrame frame = restingFrame();
        frame.right.trigger = value;
        presses += contains(mapper.update(frame, {}, kFrame).pressed, GameAction::Fire) ? 1 : 0;
    }
    CHECK(presses == 1);
}

TEST_CASE("left-handed variants produce mirrored mappings") {
    const BindingProfile right = questTouchProfile(Handedness::Right);
    const BindingProfile buttonSwap = questTouchProfile(Handedness::LeftButtonSwap);
    const BindingProfile fullSwap = questTouchProfile(Handedness::LeftButtonAndStickSwap);

    for (const ButtonBinding& binding : right.buttons) {
        if (binding.kind != PressKind::WhileDown) {
            continue;
        }
        CAPTURE(evr::game::gameActionName(binding.action));
        CHECK(contains(pressOnce(right, binding.hand, binding.input).down, binding.action));
        CHECK(contains(pressOnce(buttonSwap, buttonSwapHand(binding.hand, binding.input), binding.input).down,
                       binding.action));
        CHECK(contains(pressOnce(fullSwap, fullMirrorHand(binding.hand, binding.input), binding.input).down,
                       binding.action));
        // The unmirrored input no longer produces the action where the binding moved.
        if (buttonSwapHand(binding.hand, binding.input) != binding.hand) {
            CHECK_FALSE(contains(pressOnce(buttonSwap, binding.hand, binding.input).down, binding.action));
        }
    }
}

TEST_CASE("stick swap moves on the right stick and turns on the left") {
    InputMapper mapper(questTouchProfile(Handedness::LeftButtonAndStickSwap));
    InputFrame frame = withStick(Hand::Right, {0.0f, 1.0f});
    frame.left.stick = {1.0f, 0.0f};
    const GameInput input = mapper.update(frame, {}, kFrame);
    CHECK(input.move.y == doctest::Approx(1.0f));
    CHECK(input.turnDegrees < 0.0f);

    InputMapper rightHanded(questTouchProfile());
    const GameInput unswapped = rightHanded.update(frame, {}, kFrame);
    CHECK(unswapped.move.x == doctest::Approx(1.0f));
    CHECK(unswapped.turnDegrees == 0.0f); // Right stick straight up is the chainsaw, not a turn.
    CHECK(contains(unswapped.down, GameAction::Chainsaw));
}

TEST_CASE("snap turn fires once per flick through the mapper") {
    InputMapper mapper(questTouchProfile(), snapTurn());
    float total = 0.0f;
    int snaps = 0;
    for (int i = 0; i < 45; ++i) {
        const GameInput input = mapper.update(withStick(Hand::Right, {1.0f, 0.0f}), {}, kFrame);
        total += input.turnDegrees;
        snaps += input.turnDegrees != 0.0f ? 1 : 0;
    }
    mapper.update(restingFrame(), {}, kFrame);
    total += mapper.update(withStick(Hand::Right, {-1.0f, 0.0f}), {}, kFrame).turnDegrees;
    CHECK(snaps == 1);
    CHECK(total == doctest::Approx(0.0f));
}

TEST_CASE("no weapon switch or wheel during turning sweeps") {
    for (const TurnMode mode : {TurnMode::Smooth, TurnMode::Snap}) {
        MapperSettings settings;
        settings.turn.mode = mode;
        InputMapper mapper(questTouchProfile(), settings);
        GameActionSet everDown;
        float turned = 0.0f;
        // Ten sweeps: turn right or left, roll through the bottom of the stick, hold a while, release.
        for (int sweep = 0; sweep < 10; ++sweep) {
            const float side = sweep % 2 == 0 ? 1.0f : -1.0f;
            for (int i = 0; i <= 60; ++i) {
                const float degrees = 90.0f + 1.5f * static_cast<float>(i); // Side to straight down.
                const float radians = degrees * std::numbers::pi_v<float> / 180.0f;
                const Axis2 stick{side * std::sin(radians), std::cos(radians)};
                const GameInput input = mapper.update(withStick(Hand::Right, stick), {}, kFrame);
                everDown |= input.down;
                turned += std::fabs(input.turnDegrees);
            }
            for (int i = 0; i < 40; ++i) {
                everDown |= mapper.update(withStick(Hand::Right, {0.0f, -1.0f}), {}, kFrame).down;
            }
            everDown |= mapper.update(restingFrame(), {}, kFrame).down;
        }
        CHECK(turned > 0.0f);
        CHECK_FALSE(contains(everDown, GameAction::QuickSwitch));
        CHECK_FALSE(contains(everDown, GameAction::WeaponWheel));
        CHECK_FALSE(contains(everDown, GameAction::Chainsaw));
    }
}

TEST_CASE("stick down tap switches weapon, hold opens the wheel") {
    InputMapper mapper(questTouchProfile());
    mapper.update(withStick(Hand::Right, {0.0f, -1.0f}), {}, kFrame);
    mapper.update(withStick(Hand::Right, {0.0f, -1.0f}), {}, kFrame);
    const GameInput tap = mapper.update(restingFrame(), {}, kFrame);
    CHECK(contains(tap.pressed, GameAction::QuickSwitch));
    CHECK(contains(mapper.update(restingFrame(), {}, kFrame).released, GameAction::QuickSwitch));

    GameInput held;
    for (int i = 0; i < 40; ++i) {
        held = mapper.update(withStick(Hand::Right, {0.0f, -1.0f}), {}, kFrame);
    }
    CHECK(contains(held.down, GameAction::WeaponWheel));
    const GameInput pointing = mapper.update(withStick(Hand::Right, {-0.7f, 0.7f}), {}, kFrame);
    CHECK(contains(pointing.down, GameAction::WeaponWheel));
    CHECK(pointing.wheelPointer == Axis2{-0.7f, 0.7f});
    CHECK(pointing.turnDegrees == 0.0f);

    const GameInput closed = mapper.update(restingFrame(), {}, kFrame);
    CHECK(contains(closed.released, GameAction::WeaponWheel));
    CHECK_FALSE(contains(closed.down, GameAction::QuickSwitch));
}

TEST_CASE("face button tap and hold give different actions") {
    InputMapper mapper(questTouchProfile());
    InputFrame x = restingFrame();
    x.left.primaryButton = true;

    mapper.update(x, {}, kFrame);
    const GameInput tap = mapper.update(restingFrame(), {}, kFrame);
    CHECK(contains(tap.pressed, GameAction::SwitchEquipment));
    CHECK_FALSE(contains(tap.down, GameAction::Dossier));

    GameInput held;
    for (int i = 0; i < 30; ++i) {
        held = mapper.update(x, {}, kFrame);
    }
    CHECK(contains(held.down, GameAction::Dossier));
    const GameInput release = mapper.update(restingFrame(), {}, kFrame);
    CHECK_FALSE(contains(release.down, GameAction::SwitchEquipment));
}

TEST_CASE("the off-hand grip is Flame Belch only while the hand is free") {
    InputFrame grip = restingFrame();
    grip.left.grip = 1.0f;

    InputMapper free(questTouchProfile());
    CHECK(contains(free.update(grip, {}, kFrame).down, GameAction::FlameBelch));

    InputMapper supporting(questTouchProfile());
    MapperContext context;
    context.supportHandOnWeapon = true;
    CHECK_FALSE(contains(supporting.update(grip, context, kFrame).down, GameAction::FlameBelch));

    // The weapon hand's grip is unaffected by two-handed aiming.
    InputFrame weaponGrip = restingFrame();
    weaponGrip.right.grip = 1.0f;
    CHECK(contains(supporting.update(weaponGrip, context, kFrame).down, GameAction::WeaponMod));
}

TEST_CASE("move is rotated from the head frame into the view frame") {
    InputMapper mapper(questTouchProfile());
    InputFrame frame = withStick(Hand::Left, {0.0f, 1.0f});
    frame.head.pose = yawPose(std::numbers::pi_v<float> / 2.0f, frame.head.pose.position);
    MapperContext context;
    context.viewYawRadians = 0.0f;
    const GameInput input = mapper.update(frame, context, kFrame);
    CHECK(input.move.x == doctest::Approx(-1.0f));
    CHECK(input.move.y == doctest::Approx(0.0f).epsilon(1e-5));
}

TEST_CASE("a physical punch presses melee") {
    InputMapper mapper(questTouchProfile());
    mapper.update(restingFrame(), {}, kFrame);
    InputFrame punch = restingFrame();
    punch.right = trackedHand({0.2f, 1.3f, -0.4f}, {0.0f, 0.0f, -3.5f});
    const GameInput input = mapper.update(punch, {}, kFrame);
    CHECK(contains(input.pressed, GameAction::Melee));
}

TEST_CASE("hands jump presses jump when enabled and standing") {
    MapperSettings settings;
    settings.handsJump.enabled = true;
    InputMapper mapper(questTouchProfile(), settings);
    MapperContext context;
    context.posture = evr::posture::Posture::Standing;

    mapper.update(restingFrame(), context, kFrame);
    InputFrame up = restingFrame();
    up.left = trackedHand({-0.2f, 1.8f, -0.2f}, {0.0f, 2.5f, 0.0f});
    up.right = trackedHand({0.2f, 1.8f, -0.2f}, {0.0f, 2.5f, 0.0f});
    CHECK(contains(mapper.update(up, context, kFrame).pressed, GameAction::Jump));
}

TEST_CASE("the mapper is deterministic for a given input sequence") {
    std::vector<InputFrame> frames;
    for (int i = 0; i < 120; ++i) {
        const float t = static_cast<float>(i) * kFrame;
        InputFrame frame = restingFrame();
        frame.left.stick = {std::sin(t * 3.0f), std::cos(t * 2.0f)};
        frame.right.stick = {std::cos(t * 5.0f), std::sin(t * 7.0f)};
        frame.right.trigger = 0.5f + 0.5f * std::sin(t * 11.0f);
        frame.left.primaryButton = (i / 20) % 2 == 0;
        frames.push_back(frame);
    }
    InputMapper a(questTouchProfile(), snapTurn());
    InputMapper b(questTouchProfile(), snapTurn());
    for (const InputFrame& frame : frames) {
        const GameInput outA = a.update(frame, {}, kFrame);
        const GameInput outB = b.update(frame, {}, kFrame);
        CHECK(outA.down == outB.down);
        CHECK(outA.move == outB.move);
        CHECK(outA.turnDegrees == outB.turnDegrees);
    }
}

TEST_CASE("a non-finite frame delta counts as zero") {
    InputMapper mapper(questTouchProfile());
    const GameInput input =
        mapper.update(withStick(Hand::Right, {1.0f, 0.0f}), {}, std::numeric_limits<float>::quiet_NaN());
    CHECK(input.turnDegrees == 0.0f);
}

TEST_CASE("a remapped action works end to end") {
    // Swap jump and dash, and move the chainsaw from stick-up to the stick click.
    const auto resolved = resolveBindings(questTouchBindings(), R"(right.primary.press = "dash"
right.secondary.press = "jump"
right.stick.up = "none"
right.stick_click.press = "chainsaw"
)");
    REQUIRE(resolved.ok());
    InputMapper mapper(resolved.profile);

    InputFrame a = restingFrame();
    a.right.primaryButton = true;
    const GameInput dash = mapper.update(a, {}, kFrame);
    CHECK(contains(dash.pressed, GameAction::Dash));
    CHECK_FALSE(contains(dash.down, GameAction::Jump));

    InputFrame click = restingFrame();
    click.right.stickClick = true;
    const GameInput chainsaw = mapper.update(click, {}, kFrame);
    CHECK(contains(chainsaw.pressed, GameAction::Chainsaw));
    CHECK_FALSE(contains(chainsaw.down, GameAction::Melee));

    const GameInput up = mapper.update(withStick(Hand::Right, {0.0f, 1.0f}), {}, kFrame);
    CHECK_FALSE(contains(up.down, GameAction::Chainsaw));
}

TEST_CASE("a profile without sticks produces no movement or turning") {
    const auto resolved = resolveBindings({}, "right.trigger.press = \"fire\"\n");
    InputMapper mapper(resolved.profile);
    InputFrame frame = restingFrame();
    frame.left.stick = {0.0f, 1.0f};
    frame.right.stick = {1.0f, 0.0f};
    const GameInput input = mapper.update(frame, {}, kFrame);
    CHECK(input.move == Axis2{});
    CHECK(input.turnDegrees == 0.0f);
}

TEST_CASE("a grip held for support stays suppressed until it is released") {
    InputMapper mapper(questTouchProfile());
    InputFrame grip = restingFrame();
    grip.left.grip = 1.0f;
    MapperContext supporting;
    supporting.supportHandOnWeapon = true;
    for (int i = 0; i < 10; ++i) {
        CHECK_FALSE(contains(mapper.update(grip, supporting, kFrame).down, GameAction::FlameBelch));
    }

    // The hand leaves the fore-grip still squeezing: that squeeze was support, not Flame Belch.
    for (int i = 0; i < 10; ++i) {
        CHECK_FALSE(contains(mapper.update(grip, {}, kFrame).down, GameAction::FlameBelch));
    }
    CHECK_FALSE(contains(mapper.update(restingFrame(), {}, kFrame).down, GameAction::FlameBelch));

    // A fresh squeeze away from the weapon is Flame Belch again.
    CHECK(contains(mapper.update(grip, {}, kFrame).pressed, GameAction::FlameBelch));
}

TEST_CASE("a support grip bound to a tap does not tap when it is released") {
    const auto resolved = resolveBindings(questTouchBindings(), R"(left.grip.press = "none"
left.grip.tap = "automap"
)");
    REQUIRE(resolved.ok());
    InputMapper mapper(resolved.profile);
    InputFrame grip = restingFrame();
    grip.left.grip = 1.0f;
    MapperContext supporting;
    supporting.supportHandOnWeapon = true;
    mapper.update(grip, supporting, kFrame);
    mapper.update(grip, {}, kFrame);
    CHECK_FALSE(contains(mapper.update(restingFrame(), {}, kFrame).down, GameAction::Automap));

    // A quick squeeze away from the weapon still taps.
    mapper.update(grip, {}, kFrame);
    CHECK(contains(mapper.update(restingFrame(), {}, kFrame).down, GameAction::Automap));
}

TEST_CASE("a frame hitch does not turn a quick stick flick into a hold") {
    InputMapper mapper(questTouchProfile());
    const InputFrame down = withStick(Hand::Right, {0.0f, -1.0f});
    mapper.update(down, {}, kFrame);
    const GameInput hitch = mapper.update(down, {}, 0.4f);
    CHECK_FALSE(contains(hitch.down, GameAction::WeaponWheel));
    const GameInput released = mapper.update(restingFrame(), {}, kFrame);
    CHECK(contains(released.pressed, GameAction::QuickSwitch));
}

TEST_CASE("a frame hitch does not turn a face button tap into a hold") {
    InputMapper mapper(questTouchProfile());
    InputFrame x = restingFrame();
    x.left.primaryButton = true;
    mapper.update(x, {}, kFrame);
    CHECK_FALSE(contains(mapper.update(x, {}, 0.4f).down, GameAction::Dossier));
    CHECK(contains(mapper.update(restingFrame(), {}, kFrame).pressed, GameAction::SwitchEquipment));
}

TEST_CASE("a frame hitch turns no further than the longest counted frame") {
    InputMapper mapper(questTouchProfile());
    const InputFrame right = withStick(Hand::Right, {1.0f, 0.0f});
    const float fullRate = mapper.settings().turn.smoothDegreesPerSecond;
    const float normal = mapper.update(right, {}, kFrame).turnDegrees;
    const float hitch = mapper.update(right, {}, 2.0f).turnDegrees;
    CHECK(normal == doctest::Approx(-fullRate * kFrame));
    CHECK(hitch == doctest::Approx(-fullRate * evr::input::kMaxFrameSeconds));
}

TEST_CASE("smooth turning ramps up from zero where the turn is claimed") {
    InputMapper mapper(questTouchProfile());
    const float claim = mapper.settings().turnStick.turnClaim;
    float previousRate = 0.0f;
    float firstRate = 0.0f;
    float largestStep = 0.0f;
    // Push the stick right slowly from the centre to full deflection.
    for (int i = 0; i <= 200; ++i) {
        const float x = 0.005f * static_cast<float>(i);
        const float rate = -mapper.update(withStick(Hand::Right, {x, 0.0f}), {}, kFrame).turnDegrees / kFrame;
        if (rate > 0.0f && firstRate == 0.0f) {
            firstRate = rate;
            CHECK(x >= claim);
        }
        largestStep = std::max(largestStep, rate - previousRate);
        previousRate = rate;
    }
    CHECK(previousRate == doctest::Approx(mapper.settings().turn.smoothDegreesPerSecond));
    // No jump when the claim is made or anywhere after it.
    CHECK(firstRate < 2.0f);
    CHECK(largestStep < 5.0f);
}

TEST_CASE("invalid tuning falls back to defaults instead of producing NaN") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    MapperSettings settings;
    settings.trigger = {nan, 0.3f};
    settings.grip = {0.3f, 0.6f}; // Release above press.
    settings.buttonHoldSeconds = nan;
    settings.move = {-0.2f, 0.95f, 1.0f};
    settings.turn.smoothDegreesPerSecond = nan;
    settings.turn.smoothResponse = {0.2f, 0.95f, nan};
    settings.turnStick.centreRadius = nan;
    settings.punch.thresholdMetresPerSecond = nan;
    settings.punch.rearmFraction = -1.0f;
    InputMapper mapper(questTouchProfile(), settings);

    const MapperSettings defaults;
    CHECK(mapper.settings().trigger.press == defaults.trigger.press);
    CHECK(mapper.settings().grip.release == defaults.grip.release);
    CHECK(mapper.settings().buttonHoldSeconds == defaults.buttonHoldSeconds);
    CHECK(mapper.settings().move.deadzone == defaults.move.deadzone);
    CHECK(mapper.settings().turn.smoothDegreesPerSecond == defaults.turn.smoothDegreesPerSecond);
    CHECK(mapper.settings().turnStick.centreRadius == defaults.turnStick.centreRadius);
    CHECK(mapper.settings().punch.thresholdMetresPerSecond == defaults.punch.thresholdMetresPerSecond);
    CHECK(mapper.settings().punch.rearmFraction == defaults.punch.rearmFraction);

    // A centred stick with a negative deadzone once divided zero by zero.
    const GameInput idle = mapper.update(restingFrame(), {}, kFrame);
    CHECK(idle.move == Axis2{});
    const GameInput turning = mapper.update(withStick(Hand::Right, {1.0f, 0.0f}), {}, kFrame);
    CHECK(std::isfinite(turning.turnDegrees));
    CHECK(turning.turnDegrees < 0.0f);
}
