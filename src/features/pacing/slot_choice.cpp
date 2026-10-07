#include "features/pacing/slot_choice.hpp"

#include <algorithm>
#include <cmath>

namespace evr::pacing {

double newestWaitSeconds(double periodSeconds, double sinceFrameStartSeconds) {
    const double period =
        periodSeconds > 0.0 && std::isfinite(periodSeconds) ? periodSeconds : kDefaultPeriodSeconds;
    const double since =
        std::isfinite(sinceFrameStartSeconds) ? std::max(sinceFrameStartSeconds, 0.0) : period;
    return std::max(period - kSubmitMarginSeconds - since, 0.0);
}

SlotChoice chooseSlot(const SlotOffer& offer, double waitSeconds) {
    if (offer.newestUnshown && offer.newestRendered) {
        return SlotChoice::TakeNewest;
    }
    if (offer.newestUnshown && waitSeconds >= kMinWaitSeconds) {
        return SlotChoice::WaitNewest;
    }
    return offer.previousReady ? SlotChoice::TakePrevious : SlotChoice::Repeat;
}

} // namespace evr::pacing
