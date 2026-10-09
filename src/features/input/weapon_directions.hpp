#pragma once

// Weapon by direction: which weapon slot each of the eight stick directions picks when the thumb-rest wheel
// picks slots instead of opening the game's wheel (ETERNALVR_THUMBREST_PICK=slots, rest_wheel.hpp).
//
//   ETERNALVR_WEAPON_DIRECTIONS = "up=1,up_right=2,right=3,down_right=4,down=5,down_left=6,left=7,up_left=8"
//
// Directions are up, up_right, right, down_right, down, down_left, left and up_left (a hyphen for the
// underscore is read too); a slot is 1 to 8 (the game's weapon keys), or 0 or "none" for nothing. A
// direction the text leaves out keeps its default. The default runs clockwise from up through slots 1 to 8;
// it should be matched to the game's own wheel layout once a screenshot of the wheel on the rig confirms
// where each slot sits.

#include "features/input/wheel_mouse.hpp"
#include "game/eternal/game_action.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::input {

// The slot (1 to 8, 0 for none) of each direction, indexed by WheelDirection (Right first,
// counter-clockwise).
using WeaponDirections = std::array<std::uint8_t, 8>;

// Clockwise from up: up 1, up-right 2, right 3, down-right 4, down 5, down-left 6, left 7, up-left 8.
inline constexpr WeaponDirections kDefaultWeaponDirections{3, 2, 1, 8, 7, 6, 5, 4};

struct WeaponDirectionsResult {
    WeaponDirections table = kDefaultWeaponDirections;
    std::vector<std::string> issues; // one per entry that could not be used (that direction keeps its slot)
};

WeaponDirectionsResult parseWeaponDirections(std::string_view text);

// The WeaponSlotN action of the direction's slot, or nullopt for none (or WheelDirection::None).
std::optional<game::GameAction> weaponSlotFor(const WeaponDirections& table, WheelDirection direction);

// "up=1,up_right=2,...", clockwise from up, for the log.
std::string weaponDirectionsText(const WeaponDirections& table);

} // namespace evr::input
