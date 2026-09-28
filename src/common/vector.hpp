#pragma once

// Small vector value types.
//
// Coordinate convention used across EternalVR (the OpenXR one): right-handed, +X right, +Y up and
// -Z forward. Units are metres unless a name says otherwise.

#include <cmath>

namespace evr {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    friend constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend constexpr Vec3 operator-(Vec3 v) { return {-v.x, -v.y, -v.z}; }
    friend constexpr Vec3 operator*(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
    friend constexpr Vec3 operator*(float s, Vec3 v) { return v * s; }
    friend constexpr bool operator==(Vec3 a, Vec3 b) = default;
};

constexpr float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

inline float length(Vec3 v) {
    return std::sqrt(dot(v, v));
}

// Returns the zero vector for a zero-length input rather than dividing by zero.
inline Vec3 normalize(Vec3 v) {
    const float len = length(v);
    if (len == 0.0f) {
        return {};
    }
    return v * (1.0f / len);
}

// Homogeneous 4-component vector, used for clip-space coordinates.
struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    friend constexpr bool operator==(Vec4 a, Vec4 b) = default;
};

constexpr Vec4 toPoint(Vec3 p) {
    return {p.x, p.y, p.z, 1.0f};
}

constexpr Vec4 toDirection(Vec3 d) {
    return {d.x, d.y, d.z, 0.0f};
}

// Perspective divide. The caller is responsible for w being non-zero.
constexpr Vec3 perspectiveDivide(Vec4 v) {
    return {v.x / v.w, v.y / v.w, v.z / v.w};
}

} // namespace evr
