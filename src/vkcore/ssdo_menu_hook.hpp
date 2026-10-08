#pragma once

// The game's Directional Occlusion setting as the player last applied it, for Route S's r_SSDO hold
// (runtime_cvars.cpp, ETERNALVR_STEREO_SSDO; docs/VR_STEREO.md; Steam build 25216728).
//
// The game turns SSDO off itself while r_TAASafeMode is not 0 (r_SSDO 0 on every render, 0x1C6FCC0), so
// Route S holds r_SSDO. The player's own choice is the profile's Directional Occlusion level, which the game
// writes through its setter (RVA 0x1420F20: stores the level at settings + 0x1226C, then r_SSDO, 0 only at
// level 0, and r_SSDOQuality) at every profile load (0x14204E7), overall preset (0x142173D) and video menu
// apply (0x15E1B6E). A mid hook at the setter's entry reads the level there, so the hold follows the
// player's choice instead of putting its launch value back over it. The knock-on never calls the setter.

namespace evr::vkcore {

// From vkCreateInstance under Route S, after the multiplayer guard, before the game loads the profile.
// Locates the setter (its signature once in .text, its first cvar operand checked against r_SSDO) and hooks
// it; logs and does nothing when a check fails.
void installSsdoMenuHook();

// The r_SSDO the setting wrote last (0 or 1); -1 before it ran, or without the hook.
int ssdoMenuChoice();

} // namespace evr::vkcore
