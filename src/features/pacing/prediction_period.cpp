#include "features/pacing/prediction_period.hpp"

#include <cmath>
#include <cstdint>

namespace evr::pacing {

double predictionBaseMs(double measuredBaseMs, double runtimeMs) {
    if (std::isfinite(runtimeMs) && runtimeMs > 0.0) {
        return runtimeMs;
    }
    return std::isfinite(measuredBaseMs) && measuredBaseMs > 0.0 ? measuredBaseMs : 0.0;
}

std::int64_t predictionPeriodNs(std::int64_t reportedNs, double baseMs, bool poseLead) {
    if (poseLead || reportedNs <= 0 || !std::isfinite(baseMs) || baseMs <= 0.0) {
        return reportedNs;
    }
    const auto limit = static_cast<std::int64_t>(std::llround(baseMs * kMaxBaseMultiple * 1e6));
    return reportedNs > limit ? limit : reportedNs;
}

} // namespace evr::pacing
