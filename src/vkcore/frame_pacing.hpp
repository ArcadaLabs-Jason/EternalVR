#pragma once

// Frame pacing in the layer (ETERNALVR_PACE, features/pacing/pace_policy.hpp, docs/VR_STEREO.md "Frame
// pacing"). Three points touch it:
// - the presenter's publishSlot (presenter_ring.cpp), under the presenter's lock: an image was handed to the
//   XR worker (a stereo pair, or a mono frame), counted;
// - the XR worker's updateImage (presenter_frame.cpp), the moment it chooses the newest image for this
//   headset frame (after xrWaitFrame and xrBeginFrame): the headset began a frame; a game thread waiting for
//   it goes on;
// - the layer's vkQueuePresentKHR (swapchain_entry.cpp), after the downstream present returned and with no
//   lock of the layer held: when this present handed an image over (under Route S the present of eye R, the
//   pair complete) and pacing is on, the game's render thread waits there for the headset's next frame, at
//   most two display periods. Its next frame (the next pair) then starts at the same point of every headset
//   frame. The game's other threads are held back by the engine itself, which never runs far ahead of its
//   render thread.
// The signal comes when the image is chosen, not at xrWaitFrame's return, so an image the game finishes
// quickly cannot be taken by the same headset frame that released it (while the game keeps up every image is
// shown exactly once); and before updateImage waits for that image's copy, which waits for the game's GPU
// work: signalled after it, every game frame would start only once the previous one's GPU work was done.
//
// Off (the default) nothing waits; the counters still run, so the 10 s `pace:` line shows the cadence the
// headset got either way.
//
// Behind a runtime's menu (setBackgroundHold, pace_policy.hpp's UnfocusedCap) the same present hook holds the
// game to one image per display period on its own clock, whether pacing is on or off; while nobody can see
// the frames (features/pacing/unseen_hold.hpp, T-228) to a few images a second. Either wait ends at once when
// the hold changes. Held low, the hold switch decides nothing and the headset's frames do not wait for new
// images.

#include "features/pacing/unseen_hold.hpp"

#include <atomic>
#include <cstdint>

namespace evr::vkcore::frame_pacing {

// Reads ETERNALVR_PACE (off, headset or new) once and logs the choice; later calls return the same answer.
// True for headset.
bool configure();

// ETERNALVR_PACE=new, or headset while the game is let go (T-194; read at each headset frame, it can change):
// the XR worker begins each headset frame only once a new image has been handed over (presenter's
// waitForNewImage), so each image is submitted once. Never while the game is held low.
bool newFramesOnly();

// Presenter, under its lock: an image was handed to the XR worker.
void onHandOver();

// XR worker, once per headset frame it renders, when it chooses the newest image: the headset began a frame
// with this display period (nanoseconds); its xrWaitFrame returned at `frameStartQpc`
// (QueryPerformanceCounter) after `waitFrameMs` (release-to-present timing, present_latency.hpp, notes the
// choice first).
void onHeadsetFrame(std::int64_t periodNs, long long frameStartQpc, double waitFrameMs);

// XR worker, right before xrEndFrame (release-to-present timing).
void beforeEndFrame();

// XR worker, when a headset frame begins (before updateImage chooses), its xrWaitFrame having returned at
// `frameStartQpc` (QueryPerformanceCounter): held, waits for the image the game is still drawing, within the
// frame's own time (features/pacing/late_image.hpp; ETERNALVR_TEST_LATE_WAIT=1, a rig test); at once
// otherwise. Returns `latest` (the presenter's newest image, timeline value << 2 | slot) as read after it.
std::uint64_t awaitLateImage(const std::atomic<std::uint64_t>& latest,
                             long long frameStartQpc,
                             const std::atomic<bool>& stop);

// XR worker, after xrEndFrame: the frame showed game frame `seq` (0: none) `lateNs` after the time its pose
// was predicted for.
void noteShown(std::uint64_t seq, std::int64_t lateNs);

// The layer's present hook, after the present and outside every lock: waits for the headset's next frame
// when this present handed an image over and pacing is on, and holds the game to the background hold's rate
// (setBackgroundHold). True when it waited for a headset frame and that frame began.
bool afterPresent();

// XR worker, on session state and user presence changes and when the session ends: how far the game is held
// back while it has no viewer (pacing::ViewerWatch's answer; the rig knobs ETERNALVR_TEST_UNFOCUSED_CAP=0 and
// ETERNALVR_TEST_UNSEEN_VISIBLE=1 apply here). A change wakes a held render thread and is logged; the low
// hold's end line gives its length and rate, and a line about a second later the images handed over since.
void setBackgroundHold(pacing::UnseenHold hold);

// XR worker, every 10 s with the rates line: the `pace:` line.
void logSummary();

} // namespace evr::vkcore::frame_pacing
