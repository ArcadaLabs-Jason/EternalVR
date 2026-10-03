#pragma once

// The in-headset capture for bug reports (docs/VR_CONTROLLERS.md, docs/release/CONTROLS.md): pulling a
// trigger while the left Menu button is held (under SteamVR with Touch controllers, both sticks) asks for one
// (features/input/capture_chord.hpp), and the present hook saves the next complete eye pair as eye L and
// eye R PNG files, the game's GUI target (what the HUD quad shows) and a small text file with the head pose,
// the render size and the TAA / DLSS state, into <ETERNALVR_LOG_DIR>\captures (the launcher's Export
// report takes that folder). A menu or loading screen shows one image in both eyes: that one is saved as
// -mono.png instead. The copies are the periodic eye and UI captures' (eye_capture.hpp, ui_capture.hpp);
// the PNG files are written on a background thread.
//
// Idle cost: one relaxed atomic load per present.

#include <windows.h>

#include <cstdint>
#include <string>

namespace evr::vkcore::bug_capture {

// Captures per session: each is about 20 MB of uncompressed PNG.
inline constexpr std::uint32_t kMaxPerSession = 50;
// Under Route S a request waits this long for a stereo pair before a mono frame (a menu) is taken instead.
inline constexpr double kMonoAfterSeconds = 0.3;

// Any thread (the controllers): asks for a capture. Logged; ignored (logged once) after kMaxPerSession.
void request();

// Present hook: a capture is wanted.
bool wanted();
// Present hook: seconds since the capture was asked for (0 when none is wanted).
double secondsWaiting();
// Present hook: the capture's copies were recorded; the request is done. Its number in the session (1 on).
std::uint32_t take();
// Present hook: a capture taken above was lost (its pair was given up): the next pair is taken instead.
void retake();
// When the pending (or last taken) request was made (QPC), for the log's timing.
LONGLONG requestQpc();

// <ETERNALVR_LOG_DIR>\captures, created; empty (logged once) without a log folder or when it cannot be made.
std::wstring directory();

} // namespace evr::vkcore::bug_capture
