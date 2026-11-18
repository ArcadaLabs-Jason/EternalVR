#include "features/pacing/late_wait_gate.hpp"

#include <algorithm>
#include <cmath>

namespace evr::pacing {

LateWaitRefusal lateWaitRefusal(const LateWaitAsk& ask) {
    if (!ask.asked) {
        return LateWaitRefusal::None;
    }
    if (!ask.headset) {
        return LateWaitRefusal::NotHeadset;
    }
    if (ask.parallelEyes) {
        return LateWaitRefusal::ParallelEyes;
    }
    return ask.alternateEyes ? LateWaitRefusal::AlternateEyes : LateWaitRefusal::None;
}

bool lateWaitOn(const LateWaitAsk& ask) {
    return ask.asked && lateWaitRefusal(ask) == LateWaitRefusal::None;
}

const char* lateWaitRefusalText(LateWaitRefusal refusal) {
    switch (refusal) {
    case LateWaitRefusal::None:
        return "";
    case LateWaitRefusal::NotHeadset:
        return "frame pacing is not matched to the headset (ETERNALVR_PACE=headset)";
    case LateWaitRefusal::ParallelEyes:
        return "Parallel Eye Rendering is on";
    case LateWaitRefusal::AlternateEyes:
        return "Alternate eyes is on or auto";
    }
    return "";
}

void GpuShareEstimate::note(double fenceWaitSeconds) {
    if (!std::isfinite(fenceWaitSeconds) || fenceWaitSeconds < 0.0) {
        return;
    }
    recent_[next_] = fenceWaitSeconds;
    next_ = (next_ + 1) % kWindow;
    clean_ = fenceWaitSeconds < kCleanSeconds ? clean_ + 1 : 0;
    const double peak = *std::max_element(recent_.begin(), recent_.end());
    if (peak > held_ || clean_ >= kCleanFrames) {
        held_ = peak; // up at once; down to the recent peak only after a clean stretch
    }
}

} // namespace evr::pacing
