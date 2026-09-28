#include "features/input/stick_response.hpp"

#include "common/finite.hpp"

#include <algorithm>
#include <cmath>

namespace evr::input {

namespace {

constexpr float kMaxExponent = 10.0f;

// Maps a deflection magnitude to the response magnitude in 0..1. Written so that a centred stick
// gives zero whatever the response, since callers divide by the deflection.
float respondedMagnitude(float deflection, const StickResponse& response) {
    if (!(deflection > response.deadzone) || deflection <= 0.0f) {
        return 0.0f;
    }
    const float span = response.outerEdge - response.deadzone;
    if (span <= 0.0f) {
        return 1.0f;
    }
    const float scaled = std::clamp((deflection - response.deadzone) / span, 0.0f, 1.0f);
    return std::pow(scaled, response.exponent);
}

} // namespace

StickResponse sanitizedResponse(StickResponse response, StickResponse fallback) {
    const bool usable = finiteInRange(response.deadzone, 0.0f, 1.0f) && response.deadzone < 1.0f &&
                        finiteInRange(response.outerEdge, 0.0f, 1.0f) &&
                        response.outerEdge > response.deadzone &&
                        finiteInRange(response.exponent, 0.0f, kMaxExponent) && response.exponent > 0.0f;
    return usable ? response : fallback;
}

StickResponse startingFrom(StickResponse response, float deflection) {
    if (deflection > response.deadzone) {
        response.deadzone = deflection;
        response.outerEdge = std::max(response.outerEdge, std::min(1.0f, deflection + 0.05f));
    }
    return response;
}

Axis2 applyStickResponse(Axis2 raw, const StickResponse& response) {
    if (!isFinite(raw)) {
        return {};
    }
    const float deflection = magnitude(raw);
    const float responded = respondedMagnitude(deflection, response);
    if (responded == 0.0f) {
        return {};
    }
    return raw * (responded / deflection);
}

float applyAxisResponse(float raw, const StickResponse& response) {
    if (!std::isfinite(raw)) {
        return 0.0f;
    }
    return std::copysign(respondedMagnitude(std::fabs(raw), response), raw);
}

} // namespace evr::input
