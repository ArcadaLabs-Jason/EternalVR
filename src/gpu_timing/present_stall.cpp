#include "gpu_timing/present_stall.hpp"

#include <string>

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

namespace {

std::string checkpoints(std::uint64_t saves) {
    return saves == 1 ? std::string("a checkpoint") : std::to_string(saves) + " checkpoints";
}

} // namespace

std::string stallSaveNote(std::uint64_t saves) {
    return saves == 0 ? std::string() : "; the game saved " + checkpoints(saves) + " in the gap";
}

std::string summarySaveNote(std::uint64_t saves) {
    return saves == 0 ? std::string() : "; the game saved " + checkpoints(saves);
}

} // namespace evr::gpu_timing
