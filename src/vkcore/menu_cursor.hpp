#pragma once

// The game's menu cursor (docs/VR_MENUS.md, docs/rig-findings/menus.md): the engine's global idCursor,
// which every menu, the title screen and the in-game GUI screens use. Its `active` flag says the game
// shows the cursor (a menu is up), and its position is in the GUI's pixels. The game moves it only by
// relative mouse motion (SE_MOUSE events, 1:1 in pixels) and clamps it to the GUI's size.
//
// Read only: the layer never writes the cursor; it moves it through the game's own raw mouse input
// (key_inject.hpp). Located once, fail closed: if any check fails the menu pointer stays off.

#include <cstdint>
#include <optional>

namespace evr::vkcore::menu_cursor {

// Locates the cursor (once per process; later calls return the first answer). Only while the multiplayer
// guard is armed. False, logged, when anything does not match this build.
bool install();

struct CursorState {
    bool active = false; // the game shows its menu cursor
    std::int32_t x = 0;
    std::int32_t y = 0;
};

// The cursor now, or nullopt when it was not located, is not allocated yet, or cannot be read.
std::optional<CursorState> read();

} // namespace evr::vkcore::menu_cursor
