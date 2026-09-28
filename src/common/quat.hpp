#pragma once

// Unit quaternion for rotations. Stored in the same component order as XrQuaternionf (x, y, z, w) so
// that conversion at the OpenXR boundary is a plain member copy.

#include "common/vector.hpp"

namespace evr {

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    static constexpr Quat identity() { return {}; }

    // Rotation of `radians` about `axis`, counter-clockwise when looking down the axis toward the
    // origin (right-hand rule). The axis is normalised here so callers can pass any non-zero vector.
    static Quat fromAxisAngle(Vec3 axis, float radians);

    friend constexpr bool operator==(Quat a, Quat b) = default;
};

// Hamilton product. The result applies `b` first, then `a`, matching matrix composition order.
constexpr Quat operator*(Quat a, Quat b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

// For a unit quaternion the conjugate is the inverse rotation.
constexpr Quat conjugate(Quat q) {
    return {-q.x, -q.y, -q.z, q.w};
}

Quat normalize(Quat q);

// Rotates a vector: v' = q * v * q^-1, expanded to avoid building intermediate quaternions.
constexpr Vec3 rotate(Quat q, Vec3 v) {
    const Vec3 axis{q.x, q.y, q.z};
    const Vec3 t = 2.0f * cross(axis, v);
    return v + q.w * t + cross(axis, t);
}

} // namespace evr
