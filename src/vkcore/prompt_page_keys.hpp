#pragma once

// The screens whose A and D a stick sends (features/menu/page_keys.hpp), in build 25216728; Game
// Pass 1.0.56.0 has the same code at other addresses.
//
// The mission-complete screen's Rewards tab (idEndOfLevel_Rewards):
// - its paginator update (0xF5FBB0, Game Pass 0xF6B700), the one function that loads "paginator" and calls
//   KeyNameText, writes the arrows' keys in keyboard mode as KeyNameText(0x1E) and KeyNameText(0x20) (A, D):
//   those two calls get the page keys' text (input::KeyNameCallers::rewards, answered in prompt_hooks.cpp);
// - it sets whether the arrows are up from the page count (`setg al; mov [rsi+0x141], al` in both builds);
// - the tab's update (0xF5F170, Game Pass 0xF6ACC0), the only function that jumps to the paginator update
//   (when the input platform changes), is hooked at its start: the time it last ran and whether the arrows
//   were up then.
//
// The tab lists (idSWFWidget_TabList): their key text writer (prompt_hooks.cpp's "TabList update",
// 0x1592670) names the keyboard keys at +0x20C and +0x210 (read from its two KeyNameText calls) and counts
// the tabs at +0xA8. Their update (0x1591E70, Game Pass 0x15941F0) is hooked past its test that the tab
// list's sprite is visible: the time a visible tab list on A / D with two tabs or more last updated. The
// Dossier's Arsenal, Codex and Challenges pages set their inner tab lists on A / D (pad 0x118 / 0x119); every
// other tab list seen is on Q / E. The tab lists' KeyNameText calls already answer by the key's name, so
// their A and D get the page keys' text (input::keyNameLabel).
//
// Each part is checked at install; a part not found leaves the game's keys and text there.

#include "features/menu/page_keys.hpp"
#include "vkcore/game_text.hpp"

#include <array>
#include <cstdint>

namespace evr::vkcore::controllers {

struct PageKeyHooks {
    // The return addresses of the Rewards paginator update's two KeyNameText calls (A, D), or 0s when they
    // are not found or the tab's update cannot be hooked (the arrows then keep the game's keys).
    std::array<std::uintptr_t, 2> rewards{};
    // The tab lists' update is hooked, so a stick sends a visible tab list's A / D; when not, those keep the
    // game's text.
    bool tabLists = false;
};

// From the KeyNameText hook's install, with the tab lists' key text writer (nullptr when not found): hooks
// both.
PageKeyHooks installPageKeyHooks(const GameImage& image,
                                 const std::byte* keyNameText,
                                 const std::byte* tabListWriter,
                                 const char* tag);

// XR worker, every frame: the page keys of the screen up now (menu::pageKeysFor), for the menu router. Logs
// each change, the first 20.
menu::PageKeys currentPageKeys();

} // namespace evr::vkcore::controllers
