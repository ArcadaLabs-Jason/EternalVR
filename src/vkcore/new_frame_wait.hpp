#pragma once

// ETERNALVR_PACE=new (features/pacing/pace_policy.hpp, docs/VR_STEREO.md "Frame pacing"): the XR worker
// begins a headset frame only once the game has handed over an image not shown yet, so every image is
// submitted once and the runtime sees the game's own rate. The presenter's publishSlot signals each
// hand-over; the worker waits for it before xrWaitFrame, at most pacing::newFrameWaitSeconds (two display
// periods; the presenter then waits as long for that image's drawing on the shared fence), then begins the
// frame anyway (the previous image shown again: the game stalled, or a loading screen). The 10 s line counts
// the waits for the hand-over.

#include <atomic>
#include <cstdint>

namespace evr::vkcore::new_frame_wait {

// Presenter's publishSlot, under its lock: an image was handed to the XR worker. One SetEvent.
void onPublished();

// XR worker, before xrWaitFrame: returns once `latest` (timeline value << 2 | slot) names an image published
// after the last frame this began (not after the last one shown: an image is published at the game's present,
// before its GPU work is done, so a frame can still show the one before it, and waiting on 'not shown yet'
// would begin every refresh), after the wait limit for `periodSeconds`, or when `stop` is set.
void wait(const std::atomic<std::uint64_t>& latest, double periodSeconds, const std::atomic<bool>& stop);

// XR worker, every 10 s with the rates line: the waits of the last 10 s (nothing when none ran).
void logSummary();

} // namespace evr::vkcore::new_frame_wait
