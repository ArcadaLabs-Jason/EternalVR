#pragma once

// The menus' command bar (idSWFWidget_Button_CommandBar) names a hold button's key without a key token: the
// mission-complete screen's "ENTER HOLD TO CONTINUE" (rig, 2026-10-16). Where the button's owner has its hold
// flag set (+0x3F0) and the key is a keyboard key, the bar writes KeyNameText's text (0x17E2680 in build
// 25216728) as it is, in two places: GetKeyString() (0x159EEB0, the call at 0x159F090), found by its error
// string, and the bar's update (0x159F6B0, its "custom" layout, the call at 0x159FE8A), the one function that
// calls GetKeyString; each call must follow the hold flag's test within 0x20 bytes. The prompt hooks answer
// those two calls with the button that sends the key (prompt_hooks.cpp, input::kLayerKeyNames). The bar's
// KeyNumToString calls (0x159EA76, 0x159FB0A) build its shortcut keys, which the game matches key presses
// with, and are left alone.

#include "vkcore/game_text.hpp"

#include <cstddef>
#include <cstdint>

namespace evr::vkcore::controllers {

struct CommandBarCalls {
    std::uintptr_t getKeyString = 0; // the return address of GetKeyString's KeyNameText call, 0 if not found
    std::uintptr_t update = 0;       // the update's, 0 if not found
};

// Finds both calls to `keyNameText`, each on its own: a part that does not check out is logged under `tag`
// and left 0, and that text stays the game's.
CommandBarCalls
findCommandBarKeyNameCalls(const GameImage& image, const std::byte* keyNameText, const char* tag);

} // namespace evr::vkcore::controllers
