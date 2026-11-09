#pragma once

// Release-to-present timing (features/pacing/present_latency.hpp, docs/VR_STEREO.md "Release-to-present
// timing"): per image the game hands to the XR worker, the game's own time for it from the release before it,
// and the time from the hand-over to the xrEndFrame that submitted it, split into where it went. On with the
// performance log (CPU timing, ETERNALVR_CPU_TIMING; the launcher's PerfLog): off, every entry point returns
// at once. Every 10 s two `latency:` lines with the percentiles; ETERNALVR_TEST_LATENCY_CSV=1 (a rig test)
// also writes every image to eternalvr-latency-<pid>.csv in the log folder.

#include <cstdint>

namespace evr::vkcore::present_latency {

// CPU timing is on (read once).
bool enabled();

// Presenter's publishSlot: image `value` was handed to the XR worker.
void onHandOver(std::uint64_t value);

// The layer's present hook, after frame pacing's hold, every present: `held`, it waited for a headset frame
// that began (frame_pacing::afterPresent).
void afterPresent(bool held);

// XR worker, when a headset frame chooses its image (before the hold's signal): xrWaitFrame returned at
// `waitReturnedQpc` (QueryPerformanceCounter) after `waitFrameMs` in it.
void onChoice(long long waitReturnedQpc, double waitFrameMs);

// XR worker: the slot of image `value` was taken for the copy; the copy finished.
void onTaken(std::uint64_t value);
void onCopied(std::uint64_t value);

// XR worker, right before xrEndFrame.
void beforeEndFrame();

// XR worker, every 10 s with the rates line: the `latency:` lines.
void logSummary();

} // namespace evr::vkcore::present_latency
