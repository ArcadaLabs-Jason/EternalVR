#pragma once

// Which stick pans the Dossier's map (ETERNALVR_MAP_STICKS, docs/VR_MENUS.md). One stick pans the map and
// the other zooms (up / down) and rotates (left / right); by default the weapon hand's stick pans, and
// players who would rather pan with the other hand can swap the two. The menu router applies it
// (features/menu/menu_router.hpp); the sticks are not in the control maps, so the controls files cannot.

#include <cstdint>

namespace evr::input {

enum class MapSticks : std::uint8_t {
    WeaponPans, // the weapon hand's stick pans, the other stick zooms and rotates (default)
    OtherPans,  // the other stick pans, the weapon hand's stick zooms and rotates
};

} // namespace evr::input
