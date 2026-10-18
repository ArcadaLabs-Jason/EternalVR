#pragma once

// The hint bar's shortcuts the controllers send (features/menu/hint_keys.hpp), in build 25216728; Game Pass
// 1.0.56.0 has the same code at other addresses (GetKeyString 0x15A1230, its token call 0x15A142E, the
// button's per-frame update 0x15A1900 hooked at 0x15A1942; checked statically). A bar button
// (idSWFWidget_Button_CommandBar) holds its button's data at +0x2A0 (its keyboard key at +0x400 of that) and
// its slot at +0x2A8, as GetKeyString (0x159EEB0, found by its error string; prompt_command_bar.hpp) reads
// them.
// - GetKeyString writes a key that is not a hold button's as the key token "K_<key>" (the call to the token
//   writer at 0x159F0AE: `mov rdx, rbx; mov ecx, edi; call`), which the text pass shows as "[F]". A shortcut
//   the controllers send gets "[<button>] " there instead, the button's name in the bar's own style ("[B]
//   BACK"; the space keeps the gap the text pass leaves after a key), and the call is skipped.
// - The button's per-frame update (0x159F580: the entry after the bar's update, GetKeyString's one caller,
//   in the button's vtable; a subclass's vtable has the same update but another entry after it) tests that
//   its sprite is visible (`mov rax, [rbx+0x18]; ...; cmp byte ptr [rax+0x51], 0; jne`) and is hooked where
//   that branch goes: the time a visible button in each shortcut's slot with its key last updated.
// Both parts are checked at install; when either is missing neither is used (the bar keeps the game's keys
// and nothing sends them).

#include "features/menu/menu_router.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/prompt_command_bar.hpp"

namespace evr::vkcore::controllers {

// idStr's assignment from a C string, the game's own (prompt_hooks.cpp finds it).
using HintStrAssignFn = void (*)(void* str, const char* text);

// From the KeyNameText hook's install, with the command bar's functions as findCommandBarKeyNameCalls found
// them.
void installHintKeyHooks(const GameImage& image,
                         const CommandBarCalls& bar,
                         HintStrAssignFn strAssign,
                         const char* tag);

// XR worker, every frame, before the router: the shortcuts' buttons send their keys (menu::HintKeys) and are
// cleared in `in` while taken. Logs each shortcut's showing and going, the first 20 lines.
void updateHintKeys(menu::RouterInput& in);

// Every key down goes up (the XR frames stop, VR ends).
void releaseHintKeys();

} // namespace evr::vkcore::controllers
