#pragma once

// Artificial turning from the turn stick (R06 section 5: smooth, snap or off).
//
// Yaw deltas follow the rotation convention of the rest of the code: positive is counter-clockwise
// seen from above, about +Y, so pushing the stick right gives a negative delta.

#include "features/input/axis2.hpp"
#include "features/input/stick_response.hpp"

#include <cstdint>

namespace evr::input {

enum class TurnMode : std::uint8_t {
    Off,
    Smooth,
    Snap,
};

// Smooth-turn range offered in settings; 230 deg/s is our Recommended preset.
inline constexpr float kMinSmoothTurnDegreesPerSecond = 150.0f;
inline constexpr float kMaxSmoothTurnDegreesPerSecond = 400.0f;
// Snap angles offered are 30, 45 and 90 degrees; anything in this range is accepted.
inline constexpr float kMinSnapTurnDegrees = 15.0f;
inline constexpr float kMaxSnapTurnDegrees = 90.0f;

struct TurnSettings {
    TurnMode mode = TurnMode::Smooth;
    float smoothDegreesPerSecond = 230.0f;
    StickResponse smoothResponse = kTurnStickResponse;
    float snapDegrees = 45.0f;
    // Horizontal deflection that fires a snap.
    float snapEngage = 0.70f;
    // The stick must come back within this radius of the centre before the next snap, so one flick
    // is one snap no matter how long the stick is held or how it wanders on the way back.
    float snapRearm = 0.25f;
};

class TurnPolicy {
public:
    // Out-of-range rates and angles are clamped to the ranges above. Values that cannot be clamped
    // meaningfully fall back to the defaults: a NaN or infinite rate or angle, an unusable smooth
    // response (see sanitizedResponse), or snap thresholds outside 0-1 or with the re-arm radius not
    // below the engage deflection (both then fall back together).
    explicit TurnPolicy(TurnSettings settings = {});

    // Returns this frame's yaw delta in degrees. `turnAllowed` is false while the turn stick is being
    // used for something else (turn_stick_arbiter.hpp); a flick made while blocked is used up and does
    // not snap late. `dtSeconds` must be finite and non-negative. A non-finite stick (a lost action
    // state) turns nothing and leaves the snap state alone, so a glitch mid-flick cannot re-arm it.
    float update(Axis2 stick, float dtSeconds, bool turnAllowed);

    [[nodiscard]] const TurnSettings& settings() const { return settings_; }

private:
    float updateSnap(Axis2 stick, bool turnAllowed);

    TurnSettings settings_;
    bool snapArmed_ = true;
};

} // namespace evr::input
