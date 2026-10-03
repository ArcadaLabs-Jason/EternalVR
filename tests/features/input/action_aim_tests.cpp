#include "features/input/action_aim.hpp"
#include "features/input/controller_settings.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

using evr::game::GameAction;
using evr::game::GameActionSet;
using evr::input::ActionAim;
using evr::input::ActionAimInput;
using evr::input::ActionAimOutput;
using evr::input::ActionAimSettings;
using evr::input::ActionAimSource;
using evr::input::AimedAction;
using evr::input::kKeepSeconds;
using evr::input::kMaxHoldBackSeconds;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

GameActionSet actions(std::initializer_list<GameAction> list) {
    GameActionSet set;
    for (const GameAction a : list) {
        evr::game::add(set, a);
    }
    return set;
}

bool has(const ActionAimOutput& out, GameAction action) {
    return evr::game::contains(out.actions, action);
}

// One command: the actions held, the camera hook having written up to `written`.
ActionAimOutput step(ActionAim& aim,
                     GameActionSet held,
                     std::uint64_t written,
                     bool retargetable = true,
                     bool forced = false,
                     float dt = kFrame) {
    ActionAimInput in;
    in.actions = held;
    in.retargetable = retargetable;
    in.forcedView = forced;
    in.targetWritten = written;
    in.dtSeconds = dt;
    return aim.update(in);
}

evr::input::ControllerSettingsResult parse(const std::map<std::string, std::string, std::less<>>& values) {
    return evr::input::parseControllerSettings(
        [&values](std::string_view name) -> std::optional<std::string> {
            const auto it = values.find(name);
            if (it == values.end()) {
                return std::nullopt;
            }
            return it->second;
        });
}

} // namespace

TEST_CASE("action aim: with both on the weapon hand every action passes untouched") {
    ActionAim aim;
    const auto melee = actions({GameAction::Melee, GameAction::Fire});
    const ActionAimOutput out = step(aim, melee, 0);
    CHECK(out.actions == melee);
    CHECK(out.target == ActionAimSource::Same);
    CHECK(out.generation == 0);
    CHECK_FALSE(out.holdingBack);
}

TEST_CASE("action aim: a melee press waits for the head target, then goes out with the next command") {
    ActionAim aim({ActionAimSource::Head, ActionAimSource::Same});
    const auto melee = actions({GameAction::Melee});
    ActionAimOutput out = step(aim, {}, 0);
    CHECK(out.target == ActionAimSource::Same);
    // The press: the target turns to the head and the press is held back.
    out = step(aim, melee, 0);
    CHECK(out.target == ActionAimSource::Head);
    CHECK(out.action == AimedAction::Melee);
    CHECK(out.generation == 1);
    CHECK_FALSE(has(out, GameAction::Melee));
    CHECK(out.holdingBack);
    // The camera hook has not written it yet.
    out = step(aim, melee, 0);
    CHECK_FALSE(has(out, GameAction::Melee));
    // Written: the press goes out, one command late.
    out = step(aim, melee, 1);
    CHECK(has(out, GameAction::Melee));
    CHECK(out.released);
    CHECK_FALSE(out.timedOut);
    CHECK(out.pressed == AimedAction::Melee);
    CHECK(out.waitedCommands == 2);
    CHECK(out.waitedSeconds == doctest::Approx(2 * kFrame));
    // Held on: the press stays down.
    out = step(aim, melee, 1);
    CHECK(has(out, GameAction::Melee));
    CHECK_FALSE(out.released);
    CHECK(out.target == ActionAimSource::Head);
}

TEST_CASE("action aim: other actions are never held back") {
    ActionAim aim({ActionAimSource::Head, ActionAimSource::Head});
    const ActionAimOutput out =
        step(aim, actions({GameAction::Melee, GameAction::Fire, GameAction::Jump}), 0);
    CHECK_FALSE(has(out, GameAction::Melee));
    CHECK(has(out, GameAction::Fire));
    CHECK(has(out, GameAction::Jump));
}

TEST_CASE("action aim: a quick tap let go while it waits is still sent once") {
    ActionAim aim({ActionAimSource::OffHand, ActionAimSource::Same});
    ActionAimOutput out = step(aim, actions({GameAction::Melee}), 0);
    CHECK_FALSE(has(out, GameAction::Melee));
    out = step(aim, {}, 1);
    CHECK(has(out, GameAction::Melee));
    CHECK(out.released);
    CHECK(out.target == ActionAimSource::OffHand);
    out = step(aim, {}, 1);
    CHECK_FALSE(has(out, GameAction::Melee));
}

TEST_CASE("action aim: a press goes out after the longest wait when the view never takes the target") {
    ActionAim aim({ActionAimSource::Head, ActionAimSource::Same});
    const auto melee = actions({GameAction::Melee});
    ActionAimOutput out = step(aim, melee, 0);
    float waited = 0.0f;
    int commands = 0;
    while (!out.released && commands < 100) {
        out = step(aim, melee, 0);
        waited += kFrame;
        ++commands;
    }
    CHECK(out.released);
    CHECK(out.timedOut);
    CHECK(has(out, GameAction::Melee));
    CHECK(waited >= kMaxHoldBackSeconds);
    CHECK(waited < kMaxHoldBackSeconds + 2 * kFrame);
}

TEST_CASE("action aim: nothing is held back while the view cannot be written (forced view, menu)") {
    ActionAim aim({ActionAimSource::Head, ActionAimSource::Head});
    ActionAimOutput out = step(aim, actions({GameAction::Melee}), 0, /*retargetable=*/false, /*forced=*/true);
    CHECK(has(out, GameAction::Melee));
    CHECK_FALSE(out.holdingBack);
    // The target is still taken, for when the game lets go of the view.
    CHECK(out.target == ActionAimSource::Head);
    // A press already waiting goes out when the view stops being writable.
    ActionAim waiting({ActionAimSource::Head, ActionAimSource::Same});
    out = step(waiting, actions({GameAction::Melee}), 0);
    CHECK_FALSE(has(out, GameAction::Melee));
    out = step(waiting, actions({GameAction::Melee}), 0, /*retargetable=*/false);
    CHECK(has(out, GameAction::Melee));
    CHECK(out.released);
}

TEST_CASE("action aim: the target stays while held, through a forced view, then for the keep time") {
    ActionAim aim({ActionAimSource::Head, ActionAimSource::Same});
    const auto melee = actions({GameAction::Melee});
    step(aim, melee, 0);
    step(aim, melee, 1);
    // Held for a second: still the head.
    for (int i = 0; i < 90; ++i) {
        CHECK(step(aim, melee, 1).target == ActionAimSource::Head);
    }
    // Let go, but the game forces the view (a lunge or a glory kill) for two seconds: still the head.
    for (int i = 0; i < 180; ++i) {
        CHECK(step(aim, {}, 1, false, true).target == ActionAimSource::Head);
    }
    // Then the keep time, and back to the weapon hand.
    float kept = 0.0f;
    ActionAimOutput out = step(aim, {}, 1);
    while (!out.ended && kept < 2.0f) {
        kept += kFrame;
        out = step(aim, {}, 1);
    }
    CHECK(out.ended);
    CHECK(out.target == ActionAimSource::Same);
    CHECK(out.action == AimedAction::None);
    CHECK(out.generation == 2);
    CHECK(kept == doctest::Approx(kKeepSeconds).epsilon(0.05));
    // Going back holds nothing back.
    CHECK_FALSE(out.holdingBack);
}

TEST_CASE("action aim: the Flame Belch and the equipment launcher share the equipment source") {
    ActionAim aim({ActionAimSource::Same, ActionAimSource::OffHand});
    // Melee keeps the weapon hand: nothing changes.
    ActionAimOutput out = step(aim, actions({GameAction::Melee}), 0);
    CHECK(has(out, GameAction::Melee));
    CHECK(out.generation == 0);
    out = step(aim, {}, 0);
    out = step(aim, actions({GameAction::FlameBelch}), 0);
    CHECK_FALSE(has(out, GameAction::FlameBelch));
    CHECK(out.target == ActionAimSource::OffHand);
    CHECK(out.action == AimedAction::Equipment);
    out = step(aim, actions({GameAction::FlameBelch}), 1);
    CHECK(has(out, GameAction::FlameBelch));
    // The launcher while the Belch's target is on: no new target, nothing held back.
    out = step(aim, actions({GameAction::FlameBelch, GameAction::Equipment}), 1);
    CHECK(has(out, GameAction::Equipment));
    CHECK(out.generation == 1);
}

TEST_CASE("action aim: a press of the other action takes the target over") {
    ActionAim aim({ActionAimSource::Head, ActionAimSource::OffHand});
    step(aim, actions({GameAction::Equipment}), 0);
    ActionAimOutput out = step(aim, {}, 1);
    CHECK(out.target == ActionAimSource::OffHand);
    out = step(aim, actions({GameAction::Melee}), 1);
    CHECK(out.target == ActionAimSource::Head);
    CHECK(out.action == AimedAction::Melee);
    CHECK(out.generation == 2);
    CHECK_FALSE(has(out, GameAction::Melee));
    out = step(aim, actions({GameAction::Melee}), 2);
    CHECK(has(out, GameAction::Melee));
}

TEST_CASE("action aim: a weapon-hand press during another action's target turns the view back first") {
    ActionAim aim({ActionAimSource::Same, ActionAimSource::Head});
    step(aim, actions({GameAction::Equipment}), 0);
    ActionAimOutput out = step(aim, {}, 1);
    CHECK(out.target == ActionAimSource::Head);
    out = step(aim, actions({GameAction::Melee}), 1);
    CHECK(out.target == ActionAimSource::Same);
    CHECK(out.generation == 2);
    CHECK_FALSE(has(out, GameAction::Melee));
    out = step(aim, actions({GameAction::Melee}), 2);
    CHECK(has(out, GameAction::Melee));
    CHECK(out.pressed == AimedAction::Melee);
}

TEST_CASE("action aim: a press whose target is chosen but not written yet waits too") {
    ActionAim aim({ActionAimSource::Head, ActionAimSource::Head});
    ActionAimOutput out = step(aim, actions({GameAction::Melee}), 0);
    CHECK_FALSE(has(out, GameAction::Melee));
    // The launcher aims with the head too: no new target, but the head is not in the game's angles yet.
    out = step(aim, actions({GameAction::Melee, GameAction::Equipment}), 0);
    CHECK_FALSE(has(out, GameAction::Melee));
    CHECK_FALSE(has(out, GameAction::Equipment));
    CHECK(out.generation == 1);
    out = step(aim, actions({GameAction::Melee, GameAction::Equipment}), 1);
    CHECK(has(out, GameAction::Melee));
    CHECK(has(out, GameAction::Equipment));
    CHECK(out.released);
    // Back on the weapon hand but not written yet: a weapon-hand press waits for it.
    ActionAim back({ActionAimSource::Head, ActionAimSource::Same});
    step(back, actions({GameAction::Melee}), 0);
    step(back, {}, 1);
    out = step(back, {}, 1);
    for (int i = 0; i < 100 && !out.ended; ++i) {
        out = step(back, {}, 1);
    }
    REQUIRE(out.ended);
    CHECK(out.generation == 2);
    out = step(back, actions({GameAction::Equipment}), 1);
    CHECK_FALSE(has(out, GameAction::Equipment));
    out = step(back, actions({GameAction::Equipment}), 2);
    CHECK(has(out, GameAction::Equipment));
}

TEST_CASE("action aim: a press still waiting goes out when another press takes the target elsewhere") {
    ActionAim aim({ActionAimSource::Head, ActionAimSource::OffHand});
    ActionAimOutput out = step(aim, actions({GameAction::Melee}), 0);
    CHECK_FALSE(has(out, GameAction::Melee));
    out = step(aim, actions({GameAction::Melee, GameAction::FlameBelch}), 0);
    CHECK(has(out, GameAction::Melee));
    CHECK(out.released);
    CHECK(out.overtaken);
    CHECK(out.pressed == AimedAction::Melee);
    CHECK_FALSE(has(out, GameAction::FlameBelch));
    CHECK(out.target == ActionAimSource::OffHand);
    CHECK(out.generation == 2);
    out = step(aim, actions({GameAction::Melee, GameAction::FlameBelch}), 2);
    CHECK(has(out, GameAction::FlameBelch));
    CHECK(out.pressed == AimedAction::Equipment);
    CHECK_FALSE(out.overtaken);
}

TEST_CASE("action aim: the target stays while either action that aims with it is held") {
    ActionAim aim({ActionAimSource::Head, ActionAimSource::Head});
    step(aim, actions({GameAction::Melee}), 0);
    step(aim, actions({GameAction::Melee}), 1);
    // The launcher pressed and let go while melee stays held: the target is the launcher's now.
    step(aim, actions({GameAction::Melee, GameAction::Equipment}), 1);
    ActionAimOutput out;
    for (int i = 0; i < 90; ++i) {
        out = step(aim, actions({GameAction::Melee}), 1);
        CHECK(out.target == ActionAimSource::Head);
    }
    CHECK_FALSE(out.ended);
}

TEST_CASE("action aim: both pressed on one command: melee's target") {
    ActionAim aim({ActionAimSource::Head, ActionAimSource::OffHand});
    const ActionAimOutput out = step(aim, actions({GameAction::Melee, GameAction::Equipment}), 0);
    CHECK(out.target == ActionAimSource::Head);
    CHECK(out.action == AimedAction::Melee);
    CHECK_FALSE(has(out, GameAction::Melee));
    // The launcher pressed with it goes out: only melee's actions wait for melee's target.
    CHECK(has(out, GameAction::Equipment));
}

TEST_CASE("action aim: reset goes back to the weapon hand with a new generation") {
    ActionAim aim({ActionAimSource::Head, ActionAimSource::Same});
    step(aim, actions({GameAction::Melee}), 0);
    aim.reset();
    CHECK(aim.generation() == 2);
    const ActionAimOutput out = step(aim, {}, 0);
    CHECK(out.target == ActionAimSource::Same);
    CHECK_FALSE(out.holdingBack);
    CHECK(out.actions.none());
}

TEST_CASE("action aim: a bad time step counts as none") {
    ActionAim aim({ActionAimSource::Head, ActionAimSource::Same});
    step(aim, actions({GameAction::Melee}), 0);
    const ActionAimOutput out =
        step(aim, actions({GameAction::Melee}), 0, true, false, std::numeric_limits<float>::quiet_NaN());
    CHECK_FALSE(out.released);
    CHECK(out.holdingBack);
}

TEST_CASE("action aim settings: ETERNALVR_MELEE_AIM and ETERNALVR_EQUIPMENT_AIM") {
    const auto none = parse({});
    CHECK(none.settings.actionAim.melee == ActionAimSource::Same);
    CHECK(none.settings.actionAim.equipment == ActionAimSource::Same);
    CHECK_FALSE(none.settings.actionAim.any());
    const auto set = parse({{"ETERNALVR_MELEE_AIM", "head"}, {"ETERNALVR_EQUIPMENT_AIM", " OffHand "}});
    CHECK(set.issues.empty());
    CHECK(set.settings.actionAim.melee == ActionAimSource::Head);
    CHECK(set.settings.actionAim.equipment == ActionAimSource::OffHand);
    CHECK(set.settings.actionAim.any());
    // hand is the weapon hand, as unset; empty is unset.
    const auto hand = parse({{"ETERNALVR_MELEE_AIM", "hand"}, {"ETERNALVR_EQUIPMENT_AIM", ""}});
    CHECK(hand.issues.empty());
    CHECK_FALSE(hand.settings.actionAim.any());
    // Anything else is reported and the weapon hand kept.
    const auto bad = parse({{"ETERNALVR_EQUIPMENT_AIM", "view"}});
    REQUIRE(bad.issues.size() == 1);
    CHECK(bad.issues[0].name == "ETERNALVR_EQUIPMENT_AIM");
    CHECK(bad.issues[0].message.find("head, offhand, hand") != std::string::npos);
    CHECK(bad.settings.actionAim.equipment == ActionAimSource::Same);
}
