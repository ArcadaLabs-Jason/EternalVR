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
// Behind a runtime's menu (setUnfocused, pace_policy.hpp's UnfocusedCap) the same present hook holds the game
// to one image per display period on its own clock, whether pacing is on or off.

#include <cstdint>

namespace evr::vkcore::frame_pacing {

// Reads ETERNALVR_PACE (off or headset) once and logs the choice; later calls return the same answer.
// True for headset.
bool configure();

// Presenter, under its lock: an image was handed to the XR worker.
void onHandOver();

// XR worker, once per headset frame it renders, when it chooses the newest image: the headset began a frame
// with this display period (nanoseconds).
void onHeadsetFrame(std::int64_t periodNs);

// XR worker, after xrEndFrame: the frame showed game frame `seq` (0: none) `lateNs` after the time its pose
// was predicted for.
void noteShown(std::uint64_t seq, std::int64_t lateNs);

// The layer's present hook, after the present and outside every lock: waits for the headset's next frame
// when this present handed an image over and pacing is on, and holds the game to the cap while unfocused.
void afterPresent();

// XR worker, on session state changes and when the session ends: whether a runtime's menu is over the game
// (the session VISIBLE, or SYNCHRONIZED once hidden, after it had focus). Logged when it changes.
void setUnfocused(bool unfocused);

// XR worker, every 10 s with the rates line: the `pace:` line.
void logSummary();

} // namespace evr::vkcore::frame_pacing
