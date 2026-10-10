#pragma once

// The game's own DLSS entry in its video menu during a Route S or Parallel Eye Rendering session
// (stereo_seq/dlss_menu.hpp, docs/VR_STEREO.md "The game's video menu"; Steam build 25216728).
//
// The video settings page fills its DLSS list from the profile's index (settings + 0x122A0, read by a
// getter at RVA 0x1415E90 from the page refresh at RVA 0x15E8F30) and, when it is applied, writes the
// list's index back through the DLSS setter (RVA 0x1420FD0, called only from the page apply at RVA
// 0x15E1D8F), which stores the index and writes r_antialiasing and r_dlssQuality; the profile saved after
// it keeps the index (advDlssQualityIndex in profile.bin). The layer:
//
// - shows what runs: a mid hook right after the getter call replaces the index the list is filled with;
// - keeps the player's profile: a detour of the setter skips it when the list still holds what was shown,
//   and when the player chose another entry that the launcher's setting overrides in VR (logged); a change
//   is applied as in the flat game only when the layer follows the game's own choice (the launcher's TAA).
//
// The profile itself is never written by the layer. Every hook asks the multiplayer guard first and
// forwards to the game's code when it is not armed.

namespace evr::vkcore {

// Route S start, after the per-eye TAA hooks, or Parallel Eye Rendering's install (view_install.cpp: it holds
// TAA or each view's DLSS, view_dlss.hpp). Locates the three sites (each signature once in .text, the getter
// and setter call targets checked, the setter's r_antialiasing operand checked against the cvar) and hooks
// them; logs and does nothing when any check fails.
void installDlssMenuHooks();

} // namespace evr::vkcore
