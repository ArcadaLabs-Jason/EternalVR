#pragma once

// The layer's one-line state for the launcher: <ETERNALVR_LOG_DIR>\eternalvr-status.txt, rewritten whole on
// every change (key=value lines: state, reason, stereo, version, pid). Every refusal is in the log as well;
// this file is what the launcher shows the player, so reasons are short plain sentences.
//
//   state=starting  the layer is loaded, VR not up yet
//   state=waiting   waiting for something outside the game (the headset); VR may still come up
//   state=vr        the headset shows the game
//   state=flat      VR is off for this session; reason says why
//
// Without ETERNALVR_LOG_DIR nothing is written. A state is written only when it or its reason changes. The
// file is a temporary one moved over it; while the launcher reads it the move fails, so it is tried again a
// few times, and a write still lost is made again within a second (flush(), from the game's presents and the
// XR worker's loops).
//
// After those five keys, facts about the headset once XR is up, each written when it changes (readers skip
// keys they do not know):
//
//   runtime=SteamVR/OpenXR          the OpenXR runtime's name (xrGetInstanceProperties)
//   system=SteamVR/OpenXR : cv      the system's name (the headset model on vendor runtimes)
//   recommended=2160x2160           the runtime's recommended size per eye
//   render=1728x1728                the size of each eye's image the headset gets
//   render_planned=2016x2112        with the render size wanted: the eye size it plans (window_cap.hpp)
//   render_capped=1                 ... 1 when `render` is below it on either side (no present scaling)
//   refresh_hz=144.0                the base refresh rate (presenter_refresh.hpp)
//   throttled_share=0.181           the share of time at 2x the base or more (with each refresh summary)
//
// The start's steps, each written once per process (a later presenter or ring keeps the first time), in
// seconds on the log's clock (since the layer loaded); the launcher's start watch tells a slow start from one
// that stalls by them (StartupWatchdog.cs):
//
//   first_present=10.1              the game's first present (its window shows a picture)
//   first_headset_frame=14.3        the first game image the headset got
//
// And once the game stops presenting right after an out of date present (presenter_result.hpp):
//
//   presents=stopped_at_start       within the first 2 minutes of the headset's session (`stopped` later);
//                                   the launcher asks the player to restart the game
//   presents=resumed                ... the game presented again after that
//
// And in Route S, written when they change (runtime_cvars.cpp):
//
//   ssr_follow=1                    r_SSR is held at the game's own Reflections setting with per-eye TAA on
//   ssr_value=0                     ... the r_SSR held then, written before ssr_follow=1
//   ssdo_follow=1                   r_SSDO is held at the game's own Directional Occlusion setting, the same
//   ssdo_value=1                    ... the r_SSDO held then
//
// 0 otherwise. The launcher's settings restore keeps the r_SSR or r_SSDO the game saved only after 1, and
// only when the game saved the value held (a config last saved before the follow, as after a crash, is put
// back).
//
// And once per session in Route S or Parallel Eye Rendering (profile_hooks.hpp):
//
//   profile_repair=done             the one-time repair set view_skipShakes 1 to 0, a save wrote it
//   profile_repair=not_needed       ... asked for, the profile did not hold 1
//   profile_repair=off              ... not asked for
//
// The launcher sets its repair marker once it reads one of them.

#include <string>

namespace evr::vkcore::status {

// `reason` null: the layer is loaded and waits for the game's graphics device.
void starting(const char* reason = nullptr);
void waiting(const char* reason);
void vr(const char* what);
void flat(const char* reason);
// Whether the headset gets true stereo (on) or one image for both eyes (off, with the reason).
void stereo(bool on, const char* reason);
// Sets `key` (one of the keys above) to `value`; the file is rewritten only when it changed.
void field(const char* key, const std::string& value);
// Sets `key` (a start step above) to the log's clock now, once: a later call keeps the first time.
void reached(const char* key);
// Writes the file again when its last save was lost: at most once a second, one move with no sleep, and
// never waiting for the lock; only an atomic load otherwise (called on every present and XR frame).
void flush();

// Calls `move` up to `tries` times until it returns true, with `pause` between tries; true once it did.
template <class Move, class Pause>
bool retried(int tries, Move move, Pause pause) {
    for (int i = 0; i < tries; ++i) {
        if (i > 0) {
            pause();
        }
        if (move()) {
            return true;
        }
    }
    return false;
}

} // namespace evr::vkcore::status
