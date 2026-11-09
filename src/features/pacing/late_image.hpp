#pragma once

// Held (ETERNALVR_PACE=headset while the game is held, docs/VR_STEREO.md "Frame pacing", Late images): the
// game's frame starts when a headset frame chooses its image, and the image it draws is due when the next
// headset frame chooses, one display period later. Its own time for an image varies (release-to-present
// timing); one that runs past the period left that headset frame without a new image: the previous image went
// out again and the late one waited a whole period for the frame after. Instead, a headset frame that begins
// while the game is still drawing the image the frame before it released waits for it, within the time its
// own frame has: until kSubmitMarginSeconds plus kLateImageGpuSeconds before the period ends (counted from
// xrWaitFrame's return), leaving that much for the image's GPU work, the copy and xrEndFrame. Only while the
// game hands images over at about the headset's rate (its last hand-over at most kDrawingPeriods periods
// ago): a loading screen or a stalled game is not waited for.
//
// No OpenXR or Windows here: times are seconds.

namespace evr::pacing {

// Left after the wait for the late image's GPU work (the slot choice's fence wait) and the copy.
inline constexpr double kLateImageGpuSeconds = 0.002;
// The game counts as drawing at the headset's rate while its last hand-over is at most this many periods old.
inline constexpr double kDrawingPeriods = 3.0;

struct LateImageOffer {
    bool held = false;                   // the game is held to the headset's frames
    bool newImage = false;               // an image was handed over since the previous headset frame's choice
    double sinceHandOverSeconds = 0.0;   // since the game's last hand-over
    double sinceFrameStartSeconds = 0.0; // since this headset frame's xrWaitFrame returned
    double periodSeconds = 0.0;          // the display period (not positive or not finite: not known)
};

// How long this headset frame waits for the image the game is drawing; 0: no wait.
double lateImageWaitSeconds(const LateImageOffer& offer);

} // namespace evr::pacing
