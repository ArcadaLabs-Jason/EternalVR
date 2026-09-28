#pragma once

// Guards for tuning values that come from hand-edited settings.
//
// NaN compares false with everything, so it passes straight through std::clamp and range checks
// written as `if (x < min)`. These helpers test the valid range positively instead.

#include <cmath>

namespace evr {

// True when `value` is finite and within [min, max].
inline bool finiteInRange(float value, float min, float max) {
    return std::isfinite(value) && value >= min && value <= max;
}

// `value` when it is finite and within [min, max], otherwise `fallback`.
inline float finiteInRangeOr(float value, float min, float max, float fallback) {
    return finiteInRange(value, min, max) ? value : fallback;
}

} // namespace evr
