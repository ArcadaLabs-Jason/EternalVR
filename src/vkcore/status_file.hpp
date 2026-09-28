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

namespace evr::vkcore::status {

void starting();
void waiting(const char* reason);
void vr(const char* what);
void flat(const char* reason);
// Whether the headset gets true stereo (on) or one image for both eyes (off, with the reason).
void stereo(bool on, const char* reason);

} // namespace evr::vkcore::status
