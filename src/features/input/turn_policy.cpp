#include "features/input/turn_policy.hpp"

#include "common/finite.hpp"

#include <algorithm>
#include <cmath>

namespace evr::input {

namespace {

// NaN passes std::clamp unchanged, so non-finite values take the default before clamping.
float clampedOrDefault(float value, float min, float max, float fallback) {
    return std::clamp(std::isfinite(value) ? value : fallback, min, max);
}

TurnSettings sanitized(TurnSettings settings) {
    const TurnSettings defaults;
    settings.smoothDegreesPerSecond =
        clampedOrDefault(settings.smoothDegreesPerSecond, kMinSmoothTurnDegreesPerSecond,
                         kMaxSmoothTurnDegreesPerSecond, defaults.smoothDegreesPerSecond);
    settings.snapDegrees = clampedOrDefault(settings.snapDegrees, kMinSnapTurnDegrees, kMaxSnapTurnDegrees,
                                            defaults.snapDegrees);
    settings.smoothResponse = sanitizedResponse(settings.smoothResponse, defaults.smoothResponse);
    const bool snapThresholdsUsable = finiteInRange(settings.snapEngage, 0.0f, 1.0f) &&
                                      finiteInRange(settings.snapRearm, 0.0f, 1.0f) &&
                                      settings.snapRearm < settings.snapEngage;
    if (!snapThresholdsUsable) {
        settings.snapEngage = defaults.snapEngage;
        settings.snapRearm = defaults.snapRearm;
    }
    return settings;
}

} // namespace

TurnPolicy::TurnPolicy(TurnSettings settings) : settings_(sanitized(settings)) {}

float TurnPolicy::update(Axis2 stick, float dtSeconds, bool turnAllowed) {
    if (!isFinite(stick)) {
        return 0.0f;
    }
    switch (settings_.mode) {
    case TurnMode::Smooth:
        if (!turnAllowed) {
            return 0.0f;
        }
        return -applyAxisResponse(stick.x, settings_.smoothResponse) * settings_.smoothDegreesPerSecond *
               dtSeconds;
    case TurnMode::Snap:
        return updateSnap(stick, turnAllowed);
    case TurnMode::Off:
        break;
    }
    return 0.0f;
}

float TurnPolicy::updateSnap(Axis2 stick, bool turnAllowed) {
    if (magnitude(stick) <= settings_.snapRearm) {
        snapArmed_ = true;
        return 0.0f;
    }
    if (!snapArmed_ || std::fabs(stick.x) < settings_.snapEngage) {
        return 0.0f;
    }
    snapArmed_ = false;
    if (!turnAllowed) {
        return 0.0f;
    }
    return stick.x > 0.0f ? -settings_.snapDegrees : settings_.snapDegrees;
}

} // namespace evr::input
