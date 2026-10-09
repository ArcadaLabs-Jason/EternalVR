#include "features/input/weapon_directions.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <optional>
#include <ostream>

using evr::game::GameAction;
using evr::input::kDefaultWeaponDirections;
using evr::input::parseWeaponDirections;
using evr::input::weaponDirectionsText;
using evr::input::weaponSlotFor;
using evr::input::WheelDirection;

TEST_CASE("the default runs clockwise from up through slots 1 to 8") {
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::Up) == GameAction::WeaponSlot1);
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::UpRight) == GameAction::WeaponSlot2);
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::Right) == GameAction::WeaponSlot3);
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::DownRight) == GameAction::WeaponSlot4);
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::Down) == GameAction::WeaponSlot5);
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::DownLeft) == GameAction::WeaponSlot6);
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::Left) == GameAction::WeaponSlot7);
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::UpLeft) == GameAction::WeaponSlot8);
    CHECK_FALSE(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::None).has_value());
    CHECK(weaponDirectionsText(kDefaultWeaponDirections) ==
          "up=1,up_right=2,right=3,down_right=4,down=5,down_left=6,left=7,up_left=8");
}

TEST_CASE("a table changes the directions it names and keeps the others") {
    const auto result = parseWeaponDirections(" Up = 5, down-left=none ,left=0,right=8");
    CHECK(result.issues.empty());
    CHECK(weaponSlotFor(result.table, WheelDirection::Up) == GameAction::WeaponSlot5);
    CHECK_FALSE(weaponSlotFor(result.table, WheelDirection::DownLeft).has_value());
    CHECK_FALSE(weaponSlotFor(result.table, WheelDirection::Left).has_value());
    CHECK(weaponSlotFor(result.table, WheelDirection::Right) == GameAction::WeaponSlot8);
    CHECK(weaponSlotFor(result.table, WheelDirection::Down) == GameAction::WeaponSlot5);
    CHECK(weaponDirectionsText(result.table) ==
          "up=5,up_right=2,right=8,down_right=4,down=5,down_left=none,left=none,up_left=8");
}

TEST_CASE("entries that cannot be used are reported one by one and change nothing") {
    const auto result = parseWeaponDirections("north=1,up=9,down,left=x,,right=2");
    CHECK(result.issues.size() == 4);
    CHECK(weaponSlotFor(result.table, WheelDirection::Up) == GameAction::WeaponSlot1);
    CHECK(weaponSlotFor(result.table, WheelDirection::Right) == GameAction::WeaponSlot2);
    CHECK(parseWeaponDirections("").issues.empty());
}
