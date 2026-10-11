#pragma once

// Weapon by direction: which weapon slot each of the eight stick directions picks when the thumb-rest wheel
// picks slots instead of opening the game's wheel (ETERNALVR_THUMBREST_PICK=slots, rest_wheel.hpp).
//
//   ETERNALVR_WEAPON_DIRECTIONS = "up=1,up_right=5,right=2,down_right=7,down=3,down_left=6,left=4,up_left=8"
//
// Directions are up, up_right, right, down_right, down, down_left, left and up_left (a hyphen for the
// underscore is read too); a slot is 1 to 8 (the game's weapon keys), or 0 or "none" for no weapon: that
// direction's eighth then goes to its nearest neighbours that have a weapon (slotDirection), so four weapons
// on up, right, down and left get a quarter of the circle each. A direction the text leaves out keeps its
// default. The default is the game's own wheel (rig screenshot of the wheel with the full arsenal,
// Atlantica, 2026-10-09): clockwise from up the combat shotgun (1), super shotgun (5), heavy cannon (2),
// chaingun (7), plasma rifle (3), ballista (6), rocket launcher (4) and BFG (8), so a direction picks the
// weapon the wheel shows there.

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

// The game's wheel, clockwise from up: up 1, up-right 5, right 2, down-right 7, down 3, down-left 6, left 4,
// up-left 8.
inline constexpr WeaponDirections kDefaultWeaponDirections{2, 5, 1, 8, 4, 6, 3, 7};

struct WeaponDirectionsResult {
    WeaponDirections table = kDefaultWeaponDirections;
    std::vector<std::string> issues; // one per entry that could not be used (that direction keeps its slot)
};

WeaponDirectionsResult parseWeaponDirections(std::string_view text);

// The WeaponSlotN action of the direction's slot, or nullopt for none (or WheelDirection::None).
std::optional<game::GameAction> weaponSlotFor(const WeaponDirections& table, WheelDirection direction);

// The direction with a weapon that the stick points at: the nearest one by angle, so each reaches halfway to
// the next direction with a weapon on either side (with all eight set, the eighth of wheelDirection). While
// the stick stays within `hysteresisDegrees` past the edge of `current`'s part of the circle, `current` is
// kept. None below `threshold`, for a stick that is not finite, and when no direction has a weapon. Within an
// eighth with no weapon, an exact tie between its two neighbours goes counter-clockwise.
WheelDirection slotDirection(Axis2 stick,
                             WheelDirection current,
                             const WeaponDirections& table,
                             float threshold,
                             float hysteresisDegrees);

// "up=1,up_right=2,...", clockwise from up, for the log.
std::string weaponDirectionsText(const WeaponDirections& table);

} // namespace evr::input
