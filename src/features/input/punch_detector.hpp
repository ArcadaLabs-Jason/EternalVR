#pragma once

// Physical punch, which presses the melee action (R06 section 3.3).
//
// A punch is a hand moving along the head's forward direction faster than the threshold. Measuring
// along the head's forward, not total speed, keeps sideways sweeps of the weapon and reloading-style
// motions from punching. One punch fires per crossing: the hand has to slow below a fraction of the
// threshold before it can punch again, and a hand whose tracking starts or resumes mid-motion must
// slow down first as well. So must a hand whose punch was held back for a throw or an overhead swing
// (arm_gestures.hpp), so the end of that gesture never punches.

#include "features/input/controller_state.hpp"

#include <array>

namespace evr::input {

// Range offered in settings; 2.8 m/s is our Recommended preset.
inline constexpr float kMinPunchMetresPerSecond = 1.0f;
inline constexpr float kMaxPunchMetresPerSecond = 4.0f;

struct PunchSettings {
    bool enabled = true;
    float thresholdMetresPerSecond = 2.8f;
    float rearmFraction = 0.5f;
};

class PunchDetector {
public:
    // The threshold is clamped to the range above; a NaN or infinite one falls back to the default. A
    // re-arm fraction that is not finite or not in [0, 1] falls back to the default as well.
    explicit PunchDetector(PunchSettings settings = {});

    // Returns true on the frame either hand crosses the threshold. A hand held back (indexed by Hand)
    // never punches.
    bool update(const InputFrame& frame, const std::array<bool, 2>& heldBack = {});

    [[nodiscard]] const PunchSettings& settings() const { return settings_; }
    // The hands (indexed by Hand) that punched on the last update.
    [[nodiscard]] const std::array<bool, 2>& punched() const { return punched_; }

private:
    bool updateHand(const HandState& hand, Vec3 headForward, bool& armed) const;

    PunchSettings settings_;
    std::array<bool, 2> armed_{};
    std::array<bool, 2> punched_{};
};

} // namespace evr::input
