#pragma once

// The late-image wait's two gates (late_image.hpp, T-235): whether it runs at all, and how much of a headset
// frame it leaves for the late image's GPU work.
//
// It runs when asked for (ETERNALVR_PACE_LATE_WAIT=1) under ETERNALVR_PACE=headset with both eyes rendered
// every game frame by the standard renderer: not with Parallel Eye Rendering (its eye pairs are handed over
// differently) nor with Alternate eyes (on or auto); neither was measured, and the launcher withholds the
// switch there too.
//
// Where the graphics card sets the pace, a late image that arrives still needs more GPU time than the frame
// has left, so waiting for it only delays the frame (rig: combat at 2048 x 2208, docs/rig-findings/
// frame-tails.md). The slot choice's fence wait (the XR worker waiting after its choice for the newest
// image's GPU work) measures that need: GpuShareEstimate keeps the largest fence wait of the last kWindow
// headset frames, rises with it at once and comes down to it only after kCleanFrames frames in a row whose
// fence wait was under kCleanSeconds, and the wait leaves the larger of that and the configured share. A
// GPU-bound game keeps the estimate up and the wait steps aside; a processor-bound one's rare fence wait
// spikes hold it up for a third of a second.
//
// No OpenXR or Windows here: times are seconds.

#include <array>
#include <cstddef>

namespace evr::pacing {

enum class LateWaitRefusal {
    None,          // it runs, or was not asked for
    NotHeadset,    // ETERNALVR_PACE is not headset
    ParallelEyes,  // Parallel Eye Rendering changed the engine
    AlternateEyes, // Alternate eyes on or auto
};

struct LateWaitAsk {
    bool asked = false;         // ETERNALVR_PACE_LATE_WAIT=1
    bool headset = false;       // ETERNALVR_PACE=headset
    bool parallelEyes = false;  // Parallel Eye Rendering runs
    bool alternateEyes = false; // Alternate eyes on or auto
};

// Why the wait does not run although asked for; None when it runs or was not asked for.
LateWaitRefusal lateWaitRefusal(const LateWaitAsk& ask);
// Whether the wait runs.
bool lateWaitOn(const LateWaitAsk& ask);
// The log's words for a refusal ("" for None).
const char* lateWaitRefusalText(LateWaitRefusal refusal);

class GpuShareEstimate {
public:
    static constexpr std::size_t kWindow = 8; // headset frames the peak is taken over
    static constexpr int kCleanFrames = 30;   // frames in a row under kCleanSeconds before it comes down
    static constexpr double kCleanSeconds = 0.001;

    // A headset frame took a new image after `fenceWaitSeconds` of waiting for its GPU work (0: none), or its
    // wait ran out after that long. Not finite or negative: ignored.
    void note(double fenceWaitSeconds);
    // The GPU's recent need: the held peak (0 before any frame).
    double seconds() const { return held_; }

private:
    std::array<double, kWindow> recent_{};
    std::size_t next_ = 0;
    int clean_ = 0;
    double held_ = 0.0;
};

} // namespace evr::pacing
