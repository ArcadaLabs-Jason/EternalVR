#pragma once

// World GUIs (holograms, terminal screens) in both eyes (stereo_seq/world_gui.hpp,
// docs/rig-findings/stereo-world-gui.md; Steam build 25216728). On by default;
// ETERNALVR_STEREO_WORLD_GUI=0 turns it off, =count only counts and logs the stamps.
//
// The backend's GUI-surface check (RVA 0x1C75090, called from 0x1C74D00) draws a surface of the per-frame
// GUI buffer only when its commit stamp (`[rdx + 0xC]`) equals the world's frame number (`eax`, loaded
// from `[r8 + 0xBFD8]`). A hook on that `cmp` (RVA 0x1C750EF) gives eye R's render the stamp of eye L's
// commit in the same tick.
//
// Located by signature; anything missing leaves the game untouched and logs why.

namespace evr::vkcore {

// Locates and installs the hook once per process; later calls return the first result.
bool installWorldGuiHook();

} // namespace evr::vkcore
