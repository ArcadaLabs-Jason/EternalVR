#pragma once

// The runtime holding the XR worker in xrEndFrame (docs/VR_STEREO.md, "The runtime holding frames"). A player
// on Virtual Desktop saw 90 fps fall to the low 70s over a long session, and a trip out of the game and back
// bring it back: his logs show xrEndFrame's average growing from ~0.4 ms to 7-13 ms over an hour while
// xrWaitFrame shrank by the same amount, until the frame no longer fit in the display period. Other players
// put it down to Virtual Desktop's stream decoding on the headset (its beta restarts the stream to avoid it).
// Nothing fails and the rate falls slowly, so the 10 s `xr:` lines show it only to someone reading them; this
// watch names it:
//
// - A 10 s window holds frames when xrEndFrame's average is at least kShareOfPeriod of the display period.
// - kWindows such windows in a row begin a hold (one line); kWindows windows in a row under it end it (one
//   line, with how long it lasted). Windows with no xrEndFrame (no session) are skipped.
// - At most kMaxLines lines per watch; later holds are only counted.
//
// No OpenXR or Windows here: times are the caller's seconds on any monotonic clock.

#include <cstdint>
#include <optional>

namespace evr::pacing {

// A hold that began or ended.
struct RuntimeHold {
    bool began = false;           // false: it ended
    double endFrameMs = 0.0;      // xrEndFrame's average over the kWindows windows
    double periodMs = 0.0;        // the display period of the last window
    double lowestMs = 0.0;        // the lowest window average this watch saw
    double framesPerSecond = 0.0; // the XR frame rate over the kWindows windows
    double heldSeconds = 0.0;     // ended: from the first held window to the first window under it
};

class RuntimeHoldWatch {
public:
    static constexpr double kShareOfPeriod = 0.25;
    static constexpr int kWindows = 3;
    static constexpr std::uint32_t kMaxLines = 10;

    // One 10 s window ending at `seconds`: xrEndFrame's average and call count over it, the display period
    // (ms), and the XR worker's frame counter at its end. A hold that began or ended with this window, when
    // there is a line left for it.
    std::optional<RuntimeHold> onWindow(double endFrameMs,
                                        std::uint64_t endFrameCalls,
                                        double periodMs,
                                        std::uint64_t frames,
                                        double seconds);

    [[nodiscard]] bool holding() const { return holding_; }
    [[nodiscard]] std::uint64_t holds() const { return holds_; }

private:
    struct Window {
        double endFrameMs = 0.0;
        double framesPerSecond = 0.0;
        double started = 0.0;
    };

    bool holding_ = false;
    int streak_ = 0; // windows in a row on the other side of the line from holding_
    Window recent_[kWindows];
    double lowestMs_ = -1.0;
    double heldSince_ = 0.0;
    std::uint64_t lastFrames_ = 0;
    double lastSeconds_ = -1.0;
    std::uint64_t holds_ = 0;
    std::uint32_t lines_ = 0;
};

} // namespace evr::pacing
