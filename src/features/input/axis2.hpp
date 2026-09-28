#pragma once

// Two-axis value used for thumbsticks and planar outputs: +x right, +y forward (a stick pushed away
// from the player). Stick values lie in -1..1 on each axis.

#include <cmath>

namespace evr::input {

struct Axis2 {
    float x = 0.0f;
    float y = 0.0f;

    friend constexpr Axis2 operator*(Axis2 a, float s) { return {a.x * s, a.y * s}; }
    friend constexpr bool operator==(Axis2 a, Axis2 b) = default;
};

inline float magnitude(Axis2 a) {
    return std::hypot(a.x, a.y);
}

inline bool isFinite(Axis2 a) {
    return std::isfinite(a.x) && std::isfinite(a.y);
}

} // namespace evr::input
