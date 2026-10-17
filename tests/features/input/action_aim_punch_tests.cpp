#include "features/input/action_aim.hpp"
#include "features/input/input_frames.hpp"
#include "features/input/input_mapper.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>
#include <ostream>

// A melee press from the physical punch aims with the head under hand aim, whatever the melee setting
// (a player's 0.1.50 glory kills from a punch turned the view by up to 177 degrees); a stick-click melee
// keeps it.

using evr::game::GameAction;
using evr::game::GameActionSet;
using evr::input::ActionAim;
using evr::input::ActionAimInput;
using evr::input::ActionAimOutput;
using evr::input::ActionAimSettings;
using evr::input::ActionAimSource;
using evr::input::AimedAction;
using evr::input::kKeepSeconds;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

GameActionSet actions(std::initializer_list<GameAction> list) {
    GameActionSet set;
    for (const GameAction a : list) {
        evr::game::add(set, a);
    }
    return set;
}

bool hasMelee(const ActionAimOutput& out) {
    return evr::game::contains(out.actions, GameAction::Melee);
}

ActionAimOutput step(ActionAim& aim,
                     GameActionSet held,
                     std::uint64_t written,
                     bool punch = false,
                     bool retargetable = true,
                     bool forced = false,
                     float dt = kFrame) {
    ActionAimInput in;
    in.actions = held;
    in.meleeFromPunch = punch;
    in.retargetable = retargetable;
    in.forcedView = forced;
    in.targetWritten = written;
    in.dtSeconds = dt;
    return aim.update(in);
}

} // namespace

TEST_CASE("punch aim: a punch's melee waits for the head with melee aim on the weapon hand") {
    // Positive control: before, the policy let every press through untouched with both settings on the
    // weapon hand, so the punch's melee took the weapon hand's ray.
    ActionAim aim; // melee and equipment on the weapon hand (the default)
    const ActionAimOutput pressed = step(aim, actions({GameAction::Melee}), 0, true);
    CHECK_FALSE(hasMelee(pressed));
    CHECK(pressed.holdingBack);
    CHECK(pressed.target == ActionAimSource::Head);
    CHECK(pressed.action == AimedAction::Melee);
    CHECK(pressed.generation == 1);
    // The camera hook wrote the head target: the press goes out with the next command.
    const ActionAimOutput sent = step(aim, {}, 1);
    CHECK(hasMelee(sent));
    CHECK(sent.released);
    CHECK(sent.pressed == AimedAction::Melee);
    CHECK_FALSE(sent.timedOut);
    CHECK(sent.target == ActionAimSource::Head);
}

TEST_CASE("punch aim: a stick-click melee keeps the weapon hand") {
    ActionAim aim;
    const ActionAimOutput out = step(aim, actions({GameAction::Melee}), 0, false);
    CHECK(hasMelee(out));
    CHECK_FALSE(out.holdingBack);
    CHECK(out.target == ActionAimSource::Same);
    CHECK(out.generation == 0);
}

TEST_CASE("punch aim: a punch aims with the head with melee aim on the off hand too; the stick keeps the off "
          "hand") {
    ActionAim aim(ActionAimSettings{ActionAimSource::OffHand, ActionAimSource::Same});
    CHECK(step(aim, actions({GameAction::Melee}), 0, true).target == ActionAimSource::Head);
    step(aim, {}, 1);
    // A stick-click melee next: the off hand, held back until it is written.
    const ActionAimOutput stick = step(aim, actions({GameAction::Melee}), 1, false);
    CHECK(stick.target == ActionAimSource::OffHand);
    CHECK(stick.holdingBack);
    CHECK(stick.generation == 2);
}

TEST_CASE("punch aim: the head stays through the glory kill and the keep time, then the weapon hand") {
    ActionAim aim;
    step(aim, actions({GameAction::Melee}), 0, true);
    CHECK(hasMelee(step(aim, {}, 1)));
    // The game forces the view (the kill) for 2 s: the head stays.
    for (int i = 0; i < 180; ++i) {
        CHECK(step(aim, {}, 1, false, false, true).target == ActionAimSource::Head);
    }
    // Then the keep time, then back to the weapon hand once, with a new generation.
    ActionAimOutput out;
    int ended = 0;
    for (float t = 0.0f; t < kKeepSeconds + 0.2f; t += kFrame) {
        out = step(aim, {}, 1);
        ended += out.ended ? 1 : 0;
    }
    CHECK(ended == 1);
    CHECK(out.target == ActionAimSource::Same);
    CHECK(out.generation == 2);
    // Afterwards the weapon hand's commands pass untouched and keep the generation.
    const ActionAimOutput fire = step(aim, actions({GameAction::Fire}), 2);
    CHECK(fire.actions == actions({GameAction::Fire}));
    CHECK(fire.generation == 2);
    CHECK(fire.target == ActionAimSource::Same);
}

TEST_CASE("punch aim: a stick-click melee right after a punch turns the view back to the weapon hand first") {
    ActionAim aim;
    step(aim, actions({GameAction::Melee}), 0, true);
    step(aim, {}, 1);
    step(aim, {}, 1);
    const ActionAimOutput stick = step(aim, actions({GameAction::Melee}), 1, false);
    CHECK(stick.target == ActionAimSource::Same);
    CHECK_FALSE(hasMelee(stick));
    CHECK(stick.holdingBack);
    CHECK(hasMelee(step(aim, actions({GameAction::Melee}), 2, false)));
}

TEST_CASE("punch aim: a punch goes out at once where the view cannot be written (a menu, a forced view)") {
    ActionAim aim;
    const ActionAimOutput out = step(aim, actions({GameAction::Melee}), 0, true, false);
    CHECK(hasMelee(out));
    CHECK_FALSE(out.holdingBack);
}

TEST_CASE("punch aim: the mapper marks a melee that comes from the punch alone") {
    using evr::input::GameInput;
    using evr::input::InputFrame;
    using evr::input::InputMapper;
    using evr::test::restingFrame;
    using evr::test::trackedHand;
    InputMapper mapper(evr::test::questTouchProfile());
    mapper.update(restingFrame(), {}, kFrame);
    InputFrame punch = restingFrame();
    punch.right = trackedHand({0.2f, 1.3f, -0.4f}, {0.0f, 0.0f, -3.5f});
    const GameInput punched = mapper.update(punch, {}, kFrame);
    CHECK(evr::game::contains(punched.pressed, GameAction::Melee));
    CHECK(punched.meleeFromPunch);
    // The stick click's melee is not a punch's.
    InputMapper clicked(evr::test::questTouchProfile());
    clicked.update(restingFrame(), {}, kFrame);
    InputFrame click = restingFrame();
    click.right.stickClick = true;
    const GameInput stick = clicked.update(click, {}, kFrame);
    CHECK(evr::game::contains(stick.pressed, GameAction::Melee));
    CHECK_FALSE(stick.meleeFromPunch);
    // A punch while the stick click already holds melee is no new press.
    InputFrame both = click;
    both.right = trackedHand({0.2f, 1.3f, -0.4f}, {0.0f, 0.0f, -3.5f});
    both.right.stickClick = true;
    CHECK_FALSE(clicked.update(both, {}, kFrame).meleeFromPunch);
}

TEST_CASE("punch aim: a punch whose head target is never written goes out after the longest wait") {
    ActionAim aim;
    CHECK_FALSE(hasMelee(step(aim, actions({GameAction::Melee}), 0, true)));
    ActionAimOutput out;
    int commands = 0;
    for (; commands < 100; ++commands) {
        out = step(aim, {}, 0); // the camera hook never writes generation 1
        if (out.released) {
            break;
        }
        CHECK_FALSE(hasMelee(out));
    }
    CHECK(out.released);
    CHECK(out.timedOut);
    CHECK(hasMelee(out));
    CHECK(out.pressed == AimedAction::Melee);
    CHECK(out.waitedSeconds >= evr::input::kMaxHoldBackSeconds);
    CHECK(out.waitedSeconds < evr::input::kMaxHoldBackSeconds + 2.0f * kFrame);
    CHECK(out.target == ActionAimSource::Head); // the target stays for the forced view that may follow
}
