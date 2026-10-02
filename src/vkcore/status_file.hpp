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
// Without ETERNALVR_LOG_DIR nothing is written. A state is written only when it or its reason changes.
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

#include <string>

namespace evr::vkcore::status {

void starting();
void waiting(const char* reason);
void vr(const char* what);
void flat(const char* reason);
// Whether the headset gets true stereo (on) or one image for both eyes (off, with the reason).
void stereo(bool on, const char* reason);
// Sets `key` (one of the headset keys above) to `value`; the file is rewritten only when it changed.
void field(const char* key, const std::string& value);

} // namespace evr::vkcore::status
