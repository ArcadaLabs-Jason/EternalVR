#include "features/pacing/late_image.hpp"

#include "features/pacing/slot_choice.hpp"

#include <charconv>
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
    const double gpu =
        std::isfinite(offer.gpuSeconds) && offer.gpuSeconds >= 0.0 ? offer.gpuSeconds : kLateImageGpuSeconds;
    const double wait = period - kSubmitMarginSeconds - gpu - std::fmax(offer.sinceFrameStartSeconds, 0.0);
    return wait >= kMinWaitSeconds ? wait : 0.0;
}

double lateImageGpuSeconds(std::string_view ms) {
    double value = 0.0;
    const auto [end, error] = std::from_chars(ms.data(), ms.data() + ms.size(), value);
    if (ms.empty() || error != std::errc{} || end != ms.data() + ms.size() ||
        !(value >= 0.5 && value <= 8.0)) {
        return kLateImageGpuSeconds;
    }
    return value / 1000.0;
}

} // namespace evr::pacing
