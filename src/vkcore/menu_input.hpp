#pragma once

// Carries the menu router's decisions (features/menu/menu_router.hpp) into the game through its own raw
// input (key_inject.hpp), and tells the controllers' gameplay path when to hold back (docs/VR_MENUS.md).

#include "features/menu/menu_router.hpp"

#include <vector>

namespace evr::vkcore::menu_input {

// Posts the events in order: cursor motion, the left mouse button, the wheel and key presses. Counted for
// the statistics; the first few are logged.
void send(const std::vector<menu::RouterEvent>& events);

// While true the controllers send the game nothing but the pause key (a menu is up, or a control is still
// held from one). Any thread.
void setSuppressGameplay(bool suppress);
bool suppressGameplay();

struct Counters {
    std::uint64_t moves = 0;
    std::uint64_t clicks = 0;
    std::uint64_t wheels = 0;
    std::uint64_t keys = 0;
    std::uint64_t failed = 0; // injection refused (guard off, no window)
};
Counters counters();

} // namespace evr::vkcore::menu_input
