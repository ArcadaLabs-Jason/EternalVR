#pragma once

// The Fortress of Doom's collectible and music viewers (idHUDMenu_CollectibleViewer: the toy tables and the
// album posters), in build 25216728; Game Pass 1.0.56.0 has the same code at other addresses.
//
// The viewer's update (0xF31D90, Game Pass 0xF3D880), the one function that loads "v%d_viewer" and calls
// KeyNameText twice, names its previous and next keys in keyboard mode as KeyNameText(0x10) and
// KeyNameText(0x12), Q and E (`lea ecx, [rbx+0xE]` / `lea ecx, [rbx+0x10]` with the input platform, 2, in
// ebx); a pad gets its shoulders. The game shows its cursor over the viewer, so the menu router takes it for
// a popup, and the grips send Q and E there as in every menu: on the rig (2026-10-18) the right grip turned
// the toy table from SLAYER to ARACHNOTRON and on to ARCHVILE, the left grip back to ARACHNOTRON. Those two
// calls get the tab keys' text, the grips' names (input::KeyNameCallers::viewer, answered in
// prompt_hooks.cpp).
//
// The update is also hooked at its start, for the log: the item shown (its title's text id, which the viewer
// keeps at +0x104 and the update reads after loading "title") each time it changes, the first 40.

#include "vkcore/game_text.hpp"

#include <array>
#include <cstdint>

namespace evr::vkcore::controllers {

// The return addresses of the viewer update's two KeyNameText calls (Q, E), or 0s when they are not found
// (the viewer then keeps the game's keys).
std::array<std::uintptr_t, 2>
installViewerKeyHooks(const GameImage& image, const std::byte* keyNameText, const char* tag);

} // namespace evr::vkcore::controllers
