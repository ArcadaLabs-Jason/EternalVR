#pragma once

// Which game image the XR worker shows in a headset frame. The game publishes an image when it presents,
// before its GPU work for it is done, and runs about a frame ahead, so the newest image is often still
// rendering when the headset's frame begins. Waiting for it past the display slot (up to two periods) cost
// the slot whenever a frame took longer than a period (both eyes of Parallel Eye Rendering: skipped slots,
// judder); never waiting showed the image before it, a game frame older (pose age up a frame). So the worker
// waits for the newest image while the headset's frame still has time for it, then falls back to the one
// before it if that one was not shown yet, else shows the last image again.
//
// No OpenXR or Windows here: times are seconds.

namespace evr::pacing {

enum class SlotChoice {
    TakeNewest,   // the newest image finished rendering
    WaitNewest,   // wait for it, at most the wait given to chooseSlot, then choose again with no wait left
    TakePrevious, // the newest image published before it that finished rendering and was not shown yet
    Repeat,       // nothing new to show: the last image again
};

struct SlotOffer {
    bool newestUnshown = false;  // an image newer than the last one shown was published
    bool newestRendered = false; // ... and its game frame finished rendering
    bool previousReady = false;  // an older image newer than the last shown: rendered, not written since
};

// Kept free at the end of the headset's frame for the copy to the headset's image (about 1.5 ms when the GPU
// is busy with the game), the layers and xrEndFrame.
constexpr double kSubmitMarginSeconds = 0.004;
// A shorter wait than this is not started.
constexpr double kMinWaitSeconds = 0.001;
// While the runtime has given no display period.
constexpr double kDefaultPeriodSeconds = 1.0 / 72.0;

// How long the worker may still wait for the newest image `sinceFrameStartSeconds` after xrWaitFrame
// returned, with a display period of `periodSeconds` (not positive: none given): the rest of the period less
// kSubmitMarginSeconds, or 0.
double newestWaitSeconds(double periodSeconds, double sinceFrameStartSeconds);

// The choice for `offer` with `waitSeconds` left to wait (newestWaitSeconds; 0 after a wait).
SlotChoice chooseSlot(const SlotOffer& offer, double waitSeconds);

} // namespace evr::pacing
