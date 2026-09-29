#include "gpu_timing/present_stall.hpp"

namespace evr::gpu_timing {

StallVerdict StallGate::onGap(const PresentGap& gap) {
    if (!(gap.gapMs > kStallGapMs)) {
        return StallVerdict::None;
    }
    if (gap.ticksKnown) {
        const bool playBefore = gap.tickAgeAtStartMs >= 0.0 && gap.tickAgeAtStartMs <= kInPlayTickMs;
        if (!playBefore || !gap.tickDuring) {
            ++counters_.outsidePlay;
            return StallVerdict::OutsidePlay;
        }
    }
    ++counters_.inPlay;
    if (gap.gapMs > counters_.longestMs) {
        counters_.longestMs = gap.gapMs;
    }
    if (counters_.logged >= kStallLinesLogged) {
        return StallVerdict::Count;
    }
    ++counters_.logged;
    return counters_.logged == kStallLinesLogged ? StallVerdict::LastLog : StallVerdict::Log;
}

} // namespace evr::gpu_timing
