#pragma once

// Parallel Eye Rendering's set of the cvars the layer holds at run time (runtime_cvars.hpp: written through
// the engine's setter, through the cvar book, only while the multiplayer guard allows touching the game):
// r_useNewDepthDownscale 0 (the old depth downsample; with the new one view 1's light binning got wrong tile
// depth bounds), r_skipFlares 1, the launcher's anti-aliasing (TAA, DLSS with r_antialiasing 2 and the
// launcher's r_dlssQuality, TAA while DLSS has fallen back (view_dlss.hpp), or with ETERNALVR_STEREO_TAA=0
// r_TAASafeMode 1 and r_antialiasing 0, as the stereo set; none with ETERNALVR_STEREO_RUNTIME_CVARS=0), Route
// S's window and present set (with its render size rule) and its comfort set, r_SSR 0 with the launcher's
// Screen-space reflections Off, and r_raytracedReflectionsTemporalUpscaleQuality 0 (the launcher's command
// line value, which Route S's per-eye TAA writes back on every tick and the game's Reflections setting writes
// over at the profile's load; ETERNALVR_TEST_PE_RT_UPSCALE_HOLD=0 leaves it to the game); not Route S's
// scattering and SSDO filters (per-eye TAA is a Route S module). That hold is not the r_SSR follow's marker:
// the marker is read by per-eye TAA alone (taa_ssr.hpp), which never runs on an engine Parallel Eye Rendering
// changed, the only engine this set is held on. apply() runs from Parallel Eye Rendering's first present, on
// every present as under Route S (menus, loading screens and frames without a world included). Each hold and
// why: runtime_cvars_pe.cpp.

namespace evr::vkcore::runtime_cvars {

// What Parallel Eye Rendering's set holds, by name: a new hold is a new field, never a new positional bool.
struct ParallelEyesHolds {
    bool antiAliasingOff = false; // ETERNALVR_STEREO_TAA=0: no anti-aliasing, else TAA (or DLSS)
    bool antiAliasingHeld = true; // not ETERNALVR_STEREO_RUNTIME_CVARS=0: the anti-aliasing is held at all
    bool dlss = false;            // ETERNALVR_STEREO_DLSS=1: DLSS in both views (view_dlss.hpp), not TAA
    bool rtUpscaleHeld = true;    // not ETERNALVR_TEST_PE_RT_UPSCALE_HOLD=0: the reflections' upscale at 0
};

// Parallel Eye Rendering changed the engine (view_slots.hpp): also hold its set, as `holds` says. From its
// install, before the first apply.
void setParallelEyes(const ParallelEyesHolds& holds);

} // namespace evr::vkcore::runtime_cvars
