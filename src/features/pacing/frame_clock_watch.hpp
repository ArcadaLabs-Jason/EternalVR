#pragma once

// The XR worker against a runtime whose frame loop stops keeping time (public issue #19: SteamVR's dashboard,
// a graphics card reset behind it, then the session's predicted display time moving 1 ns per frame and a few
// frames a second for many seconds while the game presented hundreds). No xr* call fails and the runtime
// never ends the session itself, so the worker watches for it:
//
// - Stuck: the predicted display time moved on by less than half a display period per frame for kStuckFrames
//   frames in a row.
// - Slow: while the session is shown, fewer than kSlowFramesPerSecond frames a second over kSlowSeconds while
//   the game presented at least that many.
//
// The copy wait: the worker's wait for its D3D12 copy of the game's image (which waits for the game's GPU
// work) lasts at most a couple of display periods, so a slow GPU never holds the frame loop for seconds; the
// frame then shows the last image and the copy is picked up in a later frame.
//
// No OpenXR or Windows here: times are nanoseconds as the runtime gives them and the caller's seconds on any
// monotonic clock.

#include <cstdint>
#include <optional>

namespace evr::pacing {

// What the watch saw.
struct ClockStall {
    bool stuck = false;             // true: Stuck; false: Slow
    double framesPerSecond = 0.0;   // Slow: the XR frame rate over the window
    double presentsPerSecond = 0.0; // Slow: the game's present rate over it
};

class FrameClockWatch {
public:
    static constexpr int kStuckFrames = 60;
    static constexpr double kSlowFramesPerSecond = 10.0;
    static constexpr double kSlowSeconds = 5.0;

    // One frame the worker ended: its predicted display time and period (ns), when it ended, the game's
    // presents so far, and whether the session showed it (XrFrameState::shouldRender). A stall once it is
    // seen; the watch then starts over.
    std::optional<ClockStall> onFrame(std::int64_t displayTimeNs,
                                      std::int64_t periodNs,
                                      double nowSeconds,
                                      std::uint64_t presents,
                                      bool shown);
    // A new session.
    void reset() { *this = FrameClockWatch{}; }

private:
    std::optional<std::int64_t> lastDisplayTime_;
    int stuckFrames_ = 0;
    std::optional<double> windowStart_; // the slow window, while the session is shown
    std::uint64_t windowFrames_ = 0;
    std::uint64_t windowPresents_ = 0;
};

// The longest the worker waits for one copy: kCopyWaitPeriods display periods (1/72 s while the runtime has
// given none), within kMinCopyWaitMs and kMaxCopyWaitMs, in whole milliseconds.
constexpr double kCopyWaitPeriods = 2.0;
constexpr std::uint32_t kMinCopyWaitMs = 8;
constexpr std::uint32_t kMaxCopyWaitMs = 50;
std::uint32_t copyWaitMs(std::int64_t periodNs);

} // namespace evr::pacing
