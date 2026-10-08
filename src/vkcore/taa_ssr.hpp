#pragma once

// Screen-space reflections while per-eye TAA runs (taa_hooks.cpp; docs/VR_STEREO.md, ETERNALVR_STEREO_SSR).
//
// SSR keeps no history of its own: it reads the last frame's colour through the TAA history selector, so with
// per-eye history each eye reads its own, and r_SSR is held at the player's value
// (stereo_seq::stereoSsrCvar). Without it the game writes r_SSR 0 on every render itself (r_TAASafeMode 1,
// 0x1C6FCC0), which is right then: the eyes would share that colour, and nothing is held.
//
// The hold follows the game's Reflections setting (0x1421DC0), which writes r_SSR with
// r_raytracedReflectionsTemporalUpscaleQuality. Per-eye TAA holds that quality at 0 (taa_hooks.cpp reads it
// and writes it back at once), so a value from 1 to 3 read on a tick means the setting ran since the last
// tick (the profile's load, an overall preset or the player's apply in the video menu; stereo_seq::SsrHold),
// and its r_SSR is held from then on. The knock-on writes only r_SSR 0, so it is still written over. This
// needs the quality at 0 from the start: the launcher's command line sets
// +r_raytracedReflectionsTemporalUpscaleQuality 0 (its default 1 would read as Medium or higher until the
// profile's load). ETERNALVR_STEREO_SSR=off (the launcher's Screen-space reflections Off) holds 0 whatever
// the setting. Every call comes from per-eye TAA's stereo tick, except ssrFollowed.

namespace evr::vkcore {

// The first stereo tick: whether r_SSR is held (logged). `perEye`: per-eye TAA is on, not failed closed;
// `located`: the r_SSR cvar object was found.
void ssrDecide(bool perEye, bool located);

// Each stereo tick with per-eye TAA on, with the upscale quality as read before it was written back to 0 (-1
// when not located): the r_SSR value to hold, nullptr when it is not held.
const char* ssrHoldTick(int upscaleQuality);

// Per-eye TAA failed closed after it started: r_SSR is no longer held (logged once).
void ssrRelease();

// The hold follows the game's Reflections setting and that setting has run: the r_SSR the game saves is the
// player's own (the status file's ssr_follow, runtime_cvars.cpp).
bool ssrFollowed();

} // namespace evr::vkcore
