#include "features/pacing/late_image.hpp"

#include "features/pacing/slot_choice.hpp"

#include <cmath>

namespace evr::pacing {

double lateImageWaitSeconds(const LateImageOffer& offer) {
    if (!offer.held || offer.newImage) {
        return 0.0;
    }
    const double period = offer.periodSeconds;
    if (!std::isfinite(period) || period <= 0.0 || !std::isfinite(offer.sinceHandOverSeconds) ||
        !std::isfinite(offer.sinceFrameStartSeconds)) {
        return 0.0;
    }
    if (offer.sinceHandOverSeconds > kDrawingPeriods * period) {
        return 0.0; // not drawing at the headset's rate: a loading screen, a stall
    }
    const double wait =
        period - kSubmitMarginSeconds - kLateImageGpuSeconds - std::fmax(offer.sinceFrameStartSeconds, 0.0);
    return wait >= kMinWaitSeconds ? wait : 0.0;
}

} // namespace evr::pacing
