#include "xr_math/aim_check.hpp"

#include <cmath>

namespace evr::xr_math {

namespace {

// The game's view angles hold its command angles plus `delta` (to a twentieth of a degree).
bool holds(const IdAngles& view, const IdAngles& command, const IdAngles& delta) {
    return std::fabs(normalize180(view.yaw - (command.yaw + delta.yaw))) < 0.05f &&
           std::fabs(normalize180(view.pitch - (command.pitch + delta.pitch))) < 0.05f;
}

} // namespace

AimCheck::Step AimCheck::update(const IdAngles& view,
                                const IdAngles& command,
                                const IdAngles& delta,
                                const IdAngles& stateDelta,
                                bool driven) {
    Step out;
    if (phase_ == Phase::Passed || phase_ == Phase::GaveUp) {
        return out;
    }
    if (phase_ == Phase::Settling) {
        settled_ = driven ? 0 : settled_ + 1;
        if (settled_ < settleFrames()) {
            out.event = Event::Waiting;
            return out;
        }
        ++retries_;
        phase_ = Phase::Checking;
        checks_ = physicsMatches_ = stateMatches_ = mismatches_ = skipped_ = 0;
        out.event = Event::Retry;
        return out;
    }
    if (driven) {
        ++skipped_;
        out.event = Event::Skipped;
        return out;
    }
    out.counted = true;
    out.physicsMatch = holds(view, command, delta);
    out.stateMatch = holds(view, command, stateDelta);
    ++checks_;
    physicsMatches_ += out.physicsMatch ? 1 : 0;
    stateMatches_ += out.stateMatch ? 1 : 0;
    mismatches_ += (out.physicsMatch || out.stateMatch) ? 0 : 1;
    if (checks_ < kFrames) {
        out.event = Event::Counted;
    } else if (physicsMatches_ >= kNeeded || stateMatches_ >= kNeeded) {
        physics_ = physicsMatches_ >= kNeeded;
        phase_ = Phase::Passed;
        out.event = Event::Passed;
    } else if (retries_ >= kRetries) {
        phase_ = Phase::GaveUp;
        out.event = Event::GaveUp;
    } else {
        phase_ = Phase::Settling;
        settled_ = 0;
        out.event = Event::Failed;
    }
    return out;
}

} // namespace evr::xr_math
