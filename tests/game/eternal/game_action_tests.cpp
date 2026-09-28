#include "game/eternal/game_action.hpp"

#include <doctest/doctest.h>

#include <ostream>
#include <set>
#include <string_view>

using evr::game::add;
using evr::game::contains;
using evr::game::GameAction;
using evr::game::gameActionName;
using evr::game::GameActionSet;
using evr::game::kGameActionCount;

TEST_CASE("every action has a distinct name") {
    std::set<std::string_view> names;
    for (std::size_t i = 0; i < kGameActionCount; ++i) {
        const std::string_view name = gameActionName(static_cast<GameAction>(i));
        CHECK(name != "unknown");
        names.insert(name);
    }
    CHECK(names.size() == kGameActionCount);
}

TEST_CASE("action sets hold actions independently") {
    GameActionSet set;
    add(set, GameAction::Fire);
    add(set, GameAction::Automap);
    CHECK(contains(set, GameAction::Fire));
    CHECK(contains(set, GameAction::Automap));
    CHECK_FALSE(contains(set, GameAction::Jump));
    CHECK(set.count() == 2);
}

TEST_CASE("the actions that ask for a menu screen are the pause, the Dossier and its pages") {
    using evr::game::opensMenu;
    CHECK(opensMenu(GameAction::Pause));
    CHECK(opensMenu(GameAction::Dossier));
    CHECK(opensMenu(GameAction::MissionInfo));
    CHECK(opensMenu(GameAction::Automap));
    // Melee is also use: interacting with a lore object raises a popup the player did not ask for as a menu.
    CHECK_FALSE(opensMenu(GameAction::Melee));
    CHECK_FALSE(opensMenu(GameAction::Jump));
    CHECK_FALSE(opensMenu(GameAction::WeaponWheel));
    CHECK_FALSE(opensMenu(GameAction::Recenter));
}
