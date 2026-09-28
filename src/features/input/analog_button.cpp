#include "features/input/analog_button.hpp"

#include "common/finite.hpp"

#include <cmath>

namespace evr::input {

AnalogThresholds sanitizedThresholds(AnalogThresholds thresholds, AnalogThresholds fallback) {
    const bool usable = finiteInRange(thresholds.press, 0.0f, 1.0f) && thresholds.press > 0.0f &&
                        finiteInRange(thresholds.release, 0.0f, thresholds.press);
    return usable ? thresholds : fallback;
}

AnalogButton::AnalogButton(AnalogThresholds thresholds)
    : thresholds_(sanitizedThresholds(thresholds, kTriggerThresholds)) {}

ButtonState AnalogButton::update(float value) {
    const bool wasDown = down_;
    if (!std::isfinite(value)) {
        down_ = false;
    } else if (down_) {
        down_ = value > thresholds_.release;
    } else {
        down_ = value >= thresholds_.press;
    }
    return nextButtonState(wasDown, down_);
}

} // namespace evr::input
