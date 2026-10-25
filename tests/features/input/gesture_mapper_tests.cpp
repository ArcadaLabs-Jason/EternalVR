// The reach, the wrist flick and the rumble's causes through the whole mapper: what the
// command hook and the vibration take from it (usercmd_hook.cpp, haptics_xr.cpp), the gestures held back
// while the game or the weapon wheel has the input, and the weapon raised to aim pressing no use.

#include "features/input/action_aim.hpp"
#include "features/input/hand_motion.hpp"
#include "features/input/input_frames.hpp"
#include "features/input/input_mapper.hpp"
#include "features/input/rumble_route.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

using evr::game::contains;
using evr::game::GameAction;
using evr::game::Handedness;
using evr::input::ActionAim;
using evr::input::ActionAimInput;
using evr::input::ActionAimOutput;
using evr::input::ActionAimSource;
using evr::input::addRumbleCauses;
using evr::input::GameInput;
using evr::input::Hand;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::MapperContext;
using evr::input::MapperSettings;
using evr::posture::Posture;
using evr::test::HandMotion;
using evr::test::HeadPoint;
using evr::test::kMotionFrame;
using evr::test::kSeatedEyes;
using evr::test::kStandingEyes;
using evr::test::questTouchProfile;
using evr::test::restingFrame;
using evr::test::trackedHand;

namespace {

MapperSettings reachOn() {
    MapperSettings s;
    s.reach.enabled = true;
    return s;
}

HeadPoint ready(float side) {
    return {0.15f * side, -0.35f, 0.2f};
}

HeadPoint target(float side) {
    return {0.1f * side, -0.15f, 0.62f};
}

HandMotion reachMotion(Hand hand, float eyes = kStandingEyes) {
    const float side = hand == Hand::Right ? 1.0f : -1.0f;
    HandMotion m(eyes, 0.0f, ready(-1.0f), ready(1.0f));
    m.hold(0.2f).move(hand, target(side), 0.45f).hold(0.1f).move(hand, ready(side), 0.4f).hold(0.3f);
    return m;
}

// Every frame's mapper output; `context` for each frame from `contextAt` (frame index).
template <typename ContextAt>
std::vector<GameInput> play(InputMapper& mapper, const std::vector<InputFrame>& frames, ContextAt contextAt) {
    std::vector<GameInput> out;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        out.push_back(mapper.update(frames[i], contextAt(i), kMotionFrame));
    }
    return out;
}

std::vector<GameInput>
play(InputMapper& mapper, const std::vector<InputFrame>& frames, MapperContext context = {}) {
    return play(mapper, frames, [&](std::size_t) { return context; });
}

int count(const std::vector<GameInput>& inputs, GameAction action) {
    int n = 0;
    for (const GameInput& input : inputs) {
        n += contains(input.pressed, action) ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("mapper: a reach presses use alone, and a punch still presses melee") {
    InputMapper mapper(questTouchProfile(), reachOn());
    const std::vector<GameInput> reach = play(mapper, reachMotion(Hand::Left).frames);
    CHECK(count(reach, GameAction::Use) == 1);
    CHECK(count(reach, GameAction::Melee) == 0);
    int useFrames = 0;
    for (const GameInput& input : reach) {
        useFrames += contains(input.down, GameAction::Use) ? 1 : 0;
    }
    CHECK(useFrames >= 27); // held 0.3 s, as a button pressed by hand
    CHECK(useFrames <= 28);
    for (const GameInput& input : reach) {
        CHECK(input.reached == contains(input.pressed, GameAction::Use));
    }
    // A punch from the chest: melee, no use of its own.
    mapper.update(restingFrame(), {}, kMotionFrame);
    InputFrame jab = restingFrame();
    jab.right = trackedHand({0.2f, 1.3f, -0.4f}, {0.0f, 0.0f, -3.5f});
    const GameInput punched = mapper.update(jab, {}, kMotionFrame);
    CHECK(contains(punched.pressed, GameAction::Melee));
    CHECK_FALSE(contains(punched.down, GameAction::Use));
    CHECK_FALSE(punched.reached);
}

TEST_CASE("mapper: with the reach off the same motion presses nothing") {
    InputMapper mapper(questTouchProfile());
    const std::vector<GameInput> inputs = play(mapper, reachMotion(Hand::Right).frames);
    CHECK(count(inputs, GameAction::Use) == 0);
}

TEST_CASE("mapper: the weapon raised to aim presses no use, left- and right-handed, standing and seated") {
    for (const Handedness handedness : {Handedness::Right, Handedness::LeftButtonSwap}) {
        for (const float eyes : {kStandingEyes, kSeatedEyes}) {
            CAPTURE(static_cast<int>(handedness));
            CAPTURE(eyes);
            const Hand weapon = handedness == Handedness::Right ? Hand::Right : Hand::Left;
            const float side = weapon == Hand::Right ? 1.0f : -1.0f;
            const HeadPoint rest = eyes == kStandingEyes ? HeadPoint{0.22f * side, -0.72f, -0.02f}
                                                         : HeadPoint{0.18f * side, -0.45f, 0.25f};
            const HeadPoint otherRest{-rest.right, rest.up, rest.forward};
            const HeadPoint aim{0.12f * side, -0.22f, 0.6f};
            const HeadPoint support{0.02f * side, -0.28f, 0.52f};
            HandMotion m(eyes, 0.0f, weapon == Hand::Left ? rest : otherRest,
                         weapon == Hand::Right ? rest : otherRest);
            m.hold(0.3f).move(weapon, aim, 0.35f).hold(1.5f).move(weapon, rest, 0.5f).hold(0.3f);
            m.both(weapon == Hand::Left ? aim : support, weapon == Hand::Right ? aim : support, 0.4f)
                .hold(1.5f);
            m.both(weapon == Hand::Left ? rest : otherRest, weapon == Hand::Right ? rest : otherRest, 0.5f);
            MapperContext context;
            context.posture = eyes == kStandingEyes ? Posture::Standing : Posture::Seated;
            InputMapper mapper(questTouchProfile(handedness), reachOn());
            const std::vector<GameInput> inputs = play(mapper, m.frames, context);
            CHECK(count(inputs, GameAction::Use) == 0);
            // Positive control: a reach of the other hand, through the same mapper.
            HandMotion reach(eyes, 0.0f, ready(-1.0f), ready(1.0f));
            const Hand other = weapon == Hand::Right ? Hand::Left : Hand::Right;
            reach.hold(0.3f)
                .move(other, target(-side), 0.45f)
                .hold(0.1f)
                .move(other, ready(-side), 0.4f)
                .hold(0.3f);
            CHECK(count(play(mapper, reach.frames, context), GameAction::Use) == 1);
        }
    }
}

TEST_CASE("mapper: a reach's use aims with the head under hand aim, as a punch's melee does") {
    InputMapper mapper(questTouchProfile(), reachOn());
    ActionAim aim; // the melee setting left on the weapon hand
    bool sawHead = false;
    for (const InputFrame& frame : reachMotion(Hand::Left).frames) {
        const GameInput input = mapper.update(frame, {}, kMotionFrame);
        ActionAimInput in;
        in.actions = input.down;
        in.meleeFromPunch = evr::input::pressAimsWithHead(input); // as the command hook passes it
        in.retargetable = true;
        in.dtSeconds = kMotionFrame;
        const ActionAimOutput out = aim.update(in);
        if (input.reached) {
            CHECK(out.target == ActionAimSource::Head);
            CHECK(out.holdingBack); // the use waits for the view to follow the head
            CHECK_FALSE(contains(out.actions, GameAction::Use));
            sawHead = true;
        }
    }
    CHECK(sawHead);
    // A use button bound in the player's own map is no reach: it keeps the melee setting (the weapon hand).
    GameInput button;
    evr::game::add(button.down, GameAction::Use);
    CHECK_FALSE(evr::input::pressAimsWithHead(button));
}

TEST_CASE("mapper: the gestures start nothing while the game or a menu holds them back, or the wheel is up") {
    MapperSettings settings = reachOn();
    settings.flick.enabled = true;
    const std::vector<InputFrame> frames = reachMotion(Hand::Left).frames;
    // Held back while the hand is on its way out: no use, and the hand has to come back first.
    for (const int reason : {0, 1}) {
        InputMapper mapper(questTouchProfile(), settings);
        const std::vector<GameInput> inputs = play(mapper, frames, [&](std::size_t i) {
            MapperContext c;
            const bool held = i >= 35 && i < 40;
            (reason == 0 ? c.restWheelBlocked : c.menuHold) = held;
            return c;
        });
        CHECK(count(inputs, GameAction::Use) == 0);
    }
    // The weapon wheel held open on the turn stick's down hold: a reach of the other hand presses nothing.
    InputMapper wheel(questTouchProfile(), settings);
    std::vector<InputFrame> held = frames;
    for (InputFrame& frame : held) {
        frame.right.stick = {0.0f, -1.0f};
    }
    const std::vector<GameInput> inputs = play(wheel, held);
    CHECK(count(inputs, GameAction::WeaponWheel) == 1);
    CHECK(count(inputs, GameAction::Use) == 0);
    // Positive control: the same reach with nothing held back.
    InputMapper free(questTouchProfile(), settings);
    CHECK(count(play(free, frames), GameAction::Use) == 1);
}

TEST_CASE("mapper: the rumble's causes: the punching hand, the melee button's weapon hand, the launcher") {
    InputMapper mapper(questTouchProfile(), MapperSettings{});
    const auto causes = [](const GameInput& input) {
        std::array<bool, 2> melee{};
        bool shoulder = false;
        addRumbleCauses(input, Hand::Right, melee, shoulder);
        return std::pair{melee, shoulder};
    };
    // A punch of the left hand.
    mapper.update(restingFrame(), {}, kMotionFrame);
    InputFrame jab = restingFrame();
    jab.left = trackedHand({-0.2f, 1.3f, -0.4f}, {0.0f, 0.0f, -3.5f});
    const auto [punchMelee, punchShoulder] = causes(mapper.update(jab, {}, kMotionFrame));
    CHECK(punchMelee[0]);
    CHECK_FALSE(punchMelee[1]);
    CHECK_FALSE(punchShoulder);
    // The melee button (the right stick's click): the weapon hand.
    for (int i = 0; i < 60; ++i) {
        mapper.update(restingFrame(), {}, kMotionFrame);
    }
    InputFrame click = restingFrame();
    click.right.stickClick = true;
    const auto [buttonMelee, buttonShoulder] = causes(mapper.update(click, {}, kMotionFrame));
    CHECK(buttonMelee[1]);
    CHECK_FALSE(buttonMelee[0]);
    CHECK_FALSE(buttonShoulder);
    // The equipment launcher on the left trigger: the shoulder.
    mapper.update(restingFrame(), {}, kMotionFrame);
    InputFrame pull = restingFrame();
    pull.left.trigger = 1.0f;
    const GameInput launched = mapper.update(pull, {}, kMotionFrame);
    REQUIRE(contains(launched.pressed, GameAction::Equipment));
    CHECK(causes(launched).second);
}
