#include "features/input/forced_angles.hpp"

#include <algorithm>

namespace evr::input {

const char* forcedReasonName(ForcedReason reason) {
    switch (reason) {
    case ForcedReason::None:
        return "none";
    case ForcedReason::SetViewAngles:
        return "forced view angles";
    case ForcedReason::Inhibit:
        return "view inhibited";
    case ForcedReason::Cutscene:
        return "cutscene";
    case ForcedReason::Settling:
        return "settling after a forced view";
    }
    return "none";
}

ForcedAngleGate::ForcedAngleGate(int resumeFrames)
    : resumeFrames_(std::clamp(resumeFrames, 0, 600)), sinceSignal_(resumeFrames_ + 1) {}

bool ForcedAngleGate::update(const ForcedAngleSignals& signals) {
    ForcedReason now = ForcedReason::None;
    if (signals.cutscene) {
        now = ForcedReason::Cutscene;
    } else if (signals.foreignSetViewAngles) {
        now = ForcedReason::SetViewAngles;
    } else if ((signals.inhibitFlags & kInhibitViewMask) != 0) {
        now = ForcedReason::Inhibit;
    }
    const bool wasYielding = reason_ != ForcedReason::None;
    if (now != ForcedReason::None) {
        sinceSignal_ = 0;
        reason_ = now;
    } else if (sinceSignal_ < resumeFrames_) {
        ++sinceSignal_;
        reason_ = ForcedReason::Settling;
    } else {
        sinceSignal_ = resumeFrames_ + 1;
        reason_ = ForcedReason::None;
    }
    const bool yielding = reason_ != ForcedReason::None;
    if (yielding) {
        ++yielded_;
        if (!wasYielding) {
            ++episodes_;
        }
    }
    return yielding;
}

} // namespace evr::input
