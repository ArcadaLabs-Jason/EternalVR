#include "xr_math/upright_space.hpp"

#include <cmath>

namespace evr::xr_math {

namespace {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Quaternion normalised(const Quaternion& q) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(n > 1e-6f) || !std::isfinite(n)) {
        return {};
    }
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

// v rotated by the unit quaternion q.
Vec3 rotate(const Quaternion& q, const Vec3& v) {
    // t = 2 * cross(q.xyz, v); v' = v + w * t + cross(q.xyz, t)
    const Vec3 t{2.0f * (q.y * v.z - q.z * v.y), 2.0f * (q.z * v.x - q.x * v.z),
                 2.0f * (q.x * v.y - q.y * v.x)};
    return {v.x + q.w * t.x + (q.y * t.z - q.z * t.y), v.y + q.w * t.y + (q.z * t.x - q.x * t.z),
            v.z + q.w * t.z + (q.x * t.y - q.y * t.x)};
}

} // namespace

float upCosine(const Quaternion& q) {
    return rotate(normalised(q), {0.0f, 1.0f, 0.0f}).y;
}

Quaternion headingOnly(const Quaternion& q) {
    const Quaternion n = normalised(q);
    Vec3 dir = rotate(n, {0.0f, 0.0f, -1.0f});
    if (dir.x * dir.x + dir.z * dir.z < 1e-6f) {
        // Looking straight up or down: the up axis points where the top of the view faces.
        const Vec3 up = rotate(n, {0.0f, 1.0f, 0.0f});
        dir = dir.y > 0.0f ? Vec3{-up.x, 0.0f, -up.z} : Vec3{up.x, 0.0f, up.z};
        if (dir.x * dir.x + dir.z * dir.z < 1e-6f) {
            return {};
        }
    }
    // The yaw that turns -Z to (dir.x, dir.z): about +Y by atan2(-dir.x, -dir.z).
    const float yaw = std::atan2(-dir.x, -dir.z);
    return {0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)};
}

std::optional<Quaternion> uprightReplacement(const Quaternion& localInStage, float minCosine) {
    if (upCosine(localInStage) >= minCosine) {
        return std::nullopt;
    }
    return headingOnly(localInStage);
}

} // namespace evr::xr_math
