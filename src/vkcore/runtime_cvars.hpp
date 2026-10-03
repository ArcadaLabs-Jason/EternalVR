#pragma once

// Cvars the layer holds at a value while the game runs, written through the engine's own setter
// (idCVar::SetString), because the command line alone does not hold them: the game applies the player's
// settings after it (docs/VR_STEREO.md, Cvars).
//
// - Route S: the stereo set (stereo_seq::stereoRuntimeCvars: TAA off). Each eye's TAA would read the other
//   eye's history and leave a faint copy of the other eye's image in every frame. On by default under
//   Route S; ETERNALVR_STEREO_RUNTIME_CVARS=0 leaves the cvars as the game has them. With it, while per-eye
//   TAA is not requested or failed closed, r_lightScatteringTAA (stereo_seq::stereoScatterFilterCvar): 1
//   while the scattering history is per eye (scatter_hooks.hpp), else 0.
// - Route S, whatever the temporal mode: the comfort set (stereo_seq::stereoComfortCvars), which in stereo
//   only this hold sets (the launcher's command line has only r_hdrDisplay of it). apply() runs from Route
//   S's first present (presenter_copy.cpp), before the runtime's session and the first map.
// - Route S, whatever the temporal mode: the window and present set (stereo_seq::stereoWindowCvars:
//   r_fullscreen 0, r_swapInterval 0, the command line's r_windowWidth / r_windowHeight), so a load path
//   that applies the player's video mode cannot take the eyes to the display's size. Once the render size is
//   off (virtual_client::sizeOff: the surface cannot scale to it, as on an AMD driver), r_windowWidth /
//   r_windowHeight are left to the game (logged): the eyes render at the window's size, and a held size that
//   differs from it would make the game resize its window and put the swapchain out of date.
// - ETERNALVR_CPU_SAVER="name=value;name=value": the launcher's CPU Saver (launcher/data/cpu-saver.txt,
//   docs/rig-findings/perf-cpu-cvars.md), cvars that cut the CPU work of each render. A cvar the sets above
//   hold keeps their value. Unset, empty or "0": nothing.
// - ETERNALVR_SHARPENING=<number> (the launcher's Sharpening, 0 to 3): r_sharpening, the game's post-process
//   sharpening, held at that strength and compared as a float (the game's menu sets fractions such as 1.99).
//   Unset: the player's own setting.
// - A value "<=N" (in either list) is a cap: written only while the cvar's float value is above N, so a
//   player on a lower quality level keeps the game's value.
// - swf_platformOverride 2: the game's prompts stay in their keyboard form, which the layer renames to the VR
//   buttons (prompt_hooks.cpp). ETERNALVR_BUTTON_PROMPTS=0 leaves it alone.
// - Parallel Eye Rendering (setParallelEyes): r_useNewDepthDownscale 0 (the old depth downsample; with
//   the new one view 1's light binning got wrong tile depth bounds), the launcher's anti-aliasing (TAA, or
//   with ETERNALVR_STEREO_TAA=0 r_TAASafeMode 1 and r_antialiasing 0, as the stereo set; neither with
//   ETERNALVR_STEREO_RUNTIME_CVARS=0), Route S's window and present set (with its render size rule) and its
//   comfort set; not Route S's scattering filter (per-eye TAA is a Route S module). apply() runs from
//   Parallel Eye Rendering's first present, on every present as under Route S (menus, loading screens and
//   frames without a world included).
// - ETERNALVR_DEBUG_CVARS="name=value;name=value" (rig experiments); "name=?" only logs the value. An entry
//   replaces the CPU Saver's value for the same cvar.
//
// A value the game puts back is written again; each cvar's first write and the value read back are logged,
// then the first 12 later writes; the rest are counted. Only while the multiplayer guard allows touching the
// game. Every write goes through the cvar book (cvar_book.hpp): a trip sets each cvar written back to its
// value before the layer's first write, except the window set and r_hdrDisplay, which stay (logged).

#include "stereo_seq/seq_settings.hpp"

#include <cstddef>
#include <cstdint>

namespace evr::vkcore::runtime_cvars {

// A cvar registration in the game's code: lea r8, [default]; lea rdx, [name]; lea rcx, [object]; call. The
// name's lea starts at kRegistrationName and the object's at kRegistrationObject (disp32 at +3, next
// instruction at +7). Also read by game_settings.cpp.
inline constexpr const char* kRegistration =
    "4C 8D 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8";
inline constexpr std::size_t kRegistrationName = 7;
inline constexpr std::size_t kRegistrationObject = 14;

// Locates the setter and the cvars on the first call, then writes any cvar that differs from its value.
// `stereo`: Route S is on, so the stereo set applies. Cheap after the first call.
void apply(bool stereo);

// Writes so far (the first write of each cvar included).
std::uint64_t writes();

// The per-eye temporal module (per-eye TAA) reports that it is active: from then on the stereo set follows
// stereo_seq::stereoRuntimeCvars(PerEye) and the layer stops holding the TAA cvars (that module writes its
// own). The window set and ETERNALVR_DEBUG_CVARS entries stay.
void setStereoTemporal(stereo_seq::StereoTemporal temporal);

// Parallel Eye Rendering changed the engine (view_slots.hpp): also hold its set, with no anti-aliasing when
// `antiAliasingOff` (ETERNALVR_STEREO_TAA=0), else TAA; without either when not `antiAliasingHeld`
// (ETERNALVR_STEREO_RUNTIME_CVARS=0). From its install, before the first apply.
void setParallelEyes(bool antiAliasingOff, bool antiAliasingHeld);

} // namespace evr::vkcore::runtime_cvars
