#include "common/quat.hpp"

#include <cmath>

namespace evr {

Quat Quat::fromAxisAngle(Vec3 axis, float radians) {
    const Vec3 unitAxis = evr::normalize(axis);
    const float halfAngle = 0.5f * radians;
    const float s = std::sin(halfAngle);
    return {unitAxis.x * s, unitAxis.y * s, unitAxis.z * s, std::cos(halfAngle)};
}

Quat normalize(Quat q) {
    const float lengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (lengthSquared == 0.0f) {
        return Quat::identity();
    }
    const float inv = 1.0f / std::sqrt(lengthSquared);
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

} // namespace evr
