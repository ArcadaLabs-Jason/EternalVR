#pragma once

// Tolerant comparisons for the math types.

#include "common/mat4.hpp"
#include "common/quat.hpp"
#include "common/vector.hpp"

#include <cmath>
#include <cstddef>

namespace evr::test {

// Single-precision math through a few matrix products and an inverse typically lands within 1e-5
// of the exact answer for values of order one.
inline constexpr float kDefaultEpsilon = 1e-4f;

inline bool approxEqual(float a, float b, float epsilon = kDefaultEpsilon) {
    return std::fabs(a - b) <= epsilon;
}

inline bool approxEqual(Vec3 a, Vec3 b, float epsilon = kDefaultEpsilon) {
    return approxEqual(a.x, b.x, epsilon) && approxEqual(a.y, b.y, epsilon) && approxEqual(a.z, b.z, epsilon);
}

inline bool approxEqual(Vec4 a, Vec4 b, float epsilon = kDefaultEpsilon) {
    return approxEqual(a.x, b.x, epsilon) && approxEqual(a.y, b.y, epsilon) &&
           approxEqual(a.z, b.z, epsilon) && approxEqual(a.w, b.w, epsilon);
}

inline bool approxEqual(const Mat4& a, const Mat4& b, float epsilon = kDefaultEpsilon) {
    for (std::size_t i = 0; i < a.m.size(); ++i) {
        if (!approxEqual(a.m[i], b.m[i], epsilon)) {
            return false;
        }
    }
    return true;
}

} // namespace evr::test
