#pragma once

// Turns an analog trigger or grip value into a clean digital button.
//
// A single threshold chatters when a finger rests near it, and every chatter is a fresh press to the
// game (a re-fired semi-automatic weapon, a toggled weapon mod). Separate press and release
// thresholds give one press per squeeze.

#include "features/input/button_state.hpp"

namespace evr::input {

struct AnalogThresholds {
    float press = 0.55f;
    float release = 0.35f;
};

inline constexpr AnalogThresholds kTriggerThresholds{0.55f, 0.35f};
// The grip sits under the fingers whenever the controller is held, so it needs a firmer squeeze than
// the trigger before it counts.
inline constexpr AnalogThresholds kGripThresholds{0.65f, 0.40f};

// `thresholds` if usable, otherwise `fallback`. Usable means both finite, press in (0, 1], release in
// [0, 1] and release not above press. Thresholds come from settings files and must not leave a
// button stuck down or never pressable.
AnalogThresholds sanitizedThresholds(AnalogThresholds thresholds, AnalogThresholds fallback);

class AnalogButton {
public:
    // Unusable thresholds fall back to kTriggerThresholds.
    explicit AnalogButton(AnalogThresholds thresholds = kTriggerThresholds);

    // Feeds one sample in 0..1. Non-finite values (a lost action state) release the button.
    ButtonState update(float value);

    [[nodiscard]] bool isDown() const { return down_; }
    [[nodiscard]] const AnalogThresholds& thresholds() const { return thresholds_; }

private:
    AnalogThresholds thresholds_;
    bool down_ = false;
};

} // namespace evr::input
