#include "xr_math/hand_aim.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace evr::xr_math {

namespace {

constexpr float kDegreesPerRadian = 180.0f / std::numbers::pi_v<float>;

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// A vector in the body frame's local id Tech axes, expressed in the world.
Vec3 bodyToWorld(const IdViewAxis& body, Vec3 local) {
    return body.forward * local.x + body.left * local.y + body.up * local.z;
}

} // namespace

Vec3 trackingToWorld(const IdViewAxis& body, Vec3 trackingVector, float unitsPerMetre) {
    return bodyToWorld(body, openXrToIdTech(trackingVector) * unitsPerMetre);
}

WorldRay handRayInWorld(
    const IdViewAxis& body, Vec3 headWorld, const Pose& head, const Pose& aim, float unitsPerMetre) {
    WorldRay ray;
    ray.origin = headWorld + trackingToWorld(body, aim.position - head.position, unitsPerMetre);
    const Vec3 pointing = rotate(normalize(aim.orientation), Vec3{0.0f, 0.0f, -1.0f});
    ray.direction = normalize(trackingToWorld(body, pointing));
    return ray;
}

std::optional<IdAngles> anglesOfDirection(Vec3 direction) {
    if (!finite(direction) || length(direction) <= 1e-6f) {
        return std::nullopt;
    }
    const Vec3 d = normalize(direction);
    const float horizontal = std::sqrt(d.x * d.x + d.y * d.y);
    IdAngles angles;
    angles.pitch = -std::atan2(d.z, horizontal) * kDegreesPerRadian;
    angles.yaw = horizontal > 1e-6f ? std::atan2(d.y, d.x) * kDegreesPerRadian : 0.0f;
    return angles;
}

IdAngles handAimAngles(Quat aimOrientationOpenXr) {
    const Vec3 pointing = rotate(normalize(aimOrientationOpenXr), Vec3{0.0f, 0.0f, -1.0f});
    return anglesOfDirection(openXrToIdTech(pointing)).value_or(IdAngles{});
}

AimCorrection
closedLoopAim(const IdAngles& gameView, const IdAngles& target, bool forcedAngles, float pitchLimit) {
    AimCorrection correction;
    if (forcedAngles || !plausible(gameView) || !plausible(target)) {
        correction.yielded = true;
        return correction;
    }
    correction.deltaYaw = normalize180(target.yaw - gameView.yaw);
    correction.deltaPitch = std::clamp(target.pitch, -pitchLimit, pitchLimit) - gameView.pitch;
    return correction;
}

float aimErrorDegrees(const IdAngles& a, const IdAngles& b) {
    const Vec3 fa = axisFromAngles({a.pitch, a.yaw, 0.0f}).forward;
    const Vec3 fb = axisFromAngles({b.pitch, b.yaw, 0.0f}).forward;
    // atan2 of |a x b| and a . b stays accurate for tiny angles, where acos of the dot does not.
    return std::atan2(length(cross(fa, fb)), dot(fa, fb)) * kDegreesPerRadian;
}

std::optional<IdAngles> convergenceAngles(Vec3 eye, Vec3 target) {
    if (!finite(eye) || !finite(target)) {
        return std::nullopt;
    }
    return anglesOfDirection(target - eye);
}

Vec3 pointAlong(const WorldRay& ray, float distance) {
    return ray.origin + ray.direction * distance;
}

} // namespace evr::xr_math
