#pragma once

// Cvars the layer holds at a value while the game runs, written through the engine's own setter
// (idCVar::SetString), because the command line alone does not hold them: the game applies the player's
// settings after it (docs/VR_STEREO.md, Cvars).
//
// - Route S: the stereo set (stereo_seq::stereoRuntimeCvars: TAA off). Each eye's TAA would read the other
//   eye's history and leave a faint copy of the other eye's image in every frame. On by default under
//   Route S; ETERNALVR_STEREO_RUNTIME_CVARS=0 leaves the cvars as the game has them.
// - Route S, whatever the temporal mode: the window and present set (stereo_seq::stereoWindowCvars:
//   r_fullscreen 0, r_swapInterval 0, the command line's r_windowWidth / r_windowHeight), so a load path
//   that applies the player's video mode cannot take the eyes to the display's size.
// - ETERNALVR_DEBUG_CVARS="name=value;name=value" (rig experiments); "name=?" only logs the value.
//
// A value the game puts back is written again; every write and the value read back are logged (the first
// ones; later ones are counted). Only while the multiplayer guard allows touching the game.

#include "stereo_seq/seq_settings.hpp"

#include <cstdint>

namespace evr::vkcore::runtime_cvars {

// Locates the setter and the cvars on the first call, then writes any cvar that differs from its value.
// `stereo`: Route S is on, so the stereo set applies. Cheap after the first call.
void apply(bool stereo);

// Writes so far (the first write of each cvar included).
std::uint64_t writes();

// The per-eye temporal module (per-eye TAA) reports that it is active: from then on the stereo set follows
// stereo_seq::stereoRuntimeCvars(PerEye) and the layer stops holding the TAA cvars (that module writes its
// own). The window set and ETERNALVR_DEBUG_CVARS entries stay.
void setStereoTemporal(stereo_seq::StereoTemporal temporal);

} // namespace evr::vkcore::runtime_cvars
