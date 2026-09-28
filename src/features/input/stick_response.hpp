#pragma once

// Thumbstick deadzone and response curve.
//
// The deadzone is radial so diagonals behave like the cardinal directions. Deflection between the
// deadzone and the outer edge is rescaled to 0..1 and raised to `exponent`, so the output starts at
// zero right at the deadzone instead of jumping.

#include "features/input/axis2.hpp"

namespace evr::input {

struct StickResponse {
    float deadzone = 0.15f;
    // Deflection at or beyond this counts as full. Worn sticks often stop short of 1.0, especially on
    // diagonals, and partial travel feeling too slow is a common player complaint (R14 2.7).
    float outerEdge = 0.95f;
    // 1 is linear; larger values give finer control near the centre.
    float exponent = 1.0f;
};

// Movement stays linear so partial travel gives proportional speed.
inline constexpr StickResponse kMoveStickResponse{0.15f, 0.95f, 1.0f};
// Turning uses a mild curve so small corrections are easy while full deflection keeps the full rate.
inline constexpr StickResponse kTurnStickResponse{0.20f, 0.95f, 1.5f};

// `response` if usable, otherwise `fallback`. Usable means all finite, a deadzone in [0, 1), an outer
// edge beyond the deadzone and at most 1, and an exponent in (0, 10]. A negative deadzone, for one,
// would make a centred stick respond.
StickResponse sanitizedResponse(StickResponse response, StickResponse fallback);

// The same response, but starting from zero at `deflection` when that is beyond the deadzone. For
// when another rule decides the deflection at which a stick starts to count, such as the turn claim
// (turn_stick_arbiter.hpp): the output then ramps up from that point instead of jumping to the value
// the curve already has there. `response` must be usable and `deflection` below 1.
StickResponse startingFrom(StickResponse response, float deflection);

// Applies the response to a two-axis stick, keeping its direction. Non-finite input gives zero.
Axis2 applyStickResponse(Axis2 raw, const StickResponse& response);

// The same response for a single axis, keeping its sign.
float applyAxisResponse(float raw, const StickResponse& response);

} // namespace evr::input
