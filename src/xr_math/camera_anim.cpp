#include "xr_math/camera_anim.hpp"

#include "common/vector.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace evr::xr_math {

float cameraAnimSize(const IdAngles& added) {
    return std::max({std::fabs(added.pitch), std::fabs(added.yaw), std::fabs(added.roll)});
}

float cameraAnimWeight(const IdAngles& added, const CameraAnimRamp& ramp) {
    const float size = cameraAnimSize(added);
    if (!std::isfinite(size) || size <= ramp.startDegrees) {
        return 0.0f;
    }
    if (size >= ramp.fullDegrees || ramp.fullDegrees <= ramp.startDegrees) {
        return 1.0f;
    }
    const float t = (size - ramp.startDegrees) / (ramp.fullDegrees - ramp.startDegrees);
    return t * t * (3.0f - 2.0f * t);
}

IdAngles scaleAngles(const IdAngles& angles, float weight) {
    return {angles.pitch * weight, angles.yaw * weight, angles.roll * weight};
}

IdViewAxis addCameraAnim(const IdViewAxis& view, const IdAngles& added, float pitchLimit) {
    IdAngles a = anglesFromAxis(view);
    a.pitch = std::clamp(a.pitch + added.pitch, -pitchLimit, pitchLimit);
    a.yaw += added.yaw;
    a.roll += added.roll;
    return axisFromAngles(a);
}

Quat quatFromViewAxis(const IdViewAxis& axis) {
    // The rotation matrix's columns are the rotated basis vectors: forward, left, up.
    const float m00 = axis.forward.x, m10 = axis.forward.y, m20 = axis.forward.z;
    const float m01 = axis.left.x, m11 = axis.left.y, m21 = axis.left.z;
    const float m02 = axis.up.x, m12 = axis.up.y, m22 = axis.up.z;
    const float trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return normalize(q);
}

Quat headWithCameraAnim(const IdViewAxis& body, Quat headInIdTech, const IdAngles& added) {
    const IdViewAxis view = addCameraAnim(composeHeadAxis(body, headInIdTech), added);
    // The view in the body frame: each row's components along the body's axes.
    const auto local = [&body](Vec3 world) {
        return Vec3{dot(world, body.forward), dot(world, body.left), dot(world, body.up)};
    };
    return quatFromViewAxis({local(view.forward), local(view.left), local(view.up)});
}

std::optional<IdViewAxis>
removeCameraAnim(const IdViewAxis& gameAxis, const IdAngles& added, float toleranceDegrees) {
    const IdAngles game = anglesFromAxis(gameAxis);
    const IdViewAxis base =
        axisFromAngles({game.pitch - added.pitch, game.yaw - added.yaw, game.roll - added.roll});
    // Adding the animation back must give the game's view (without the clamp, which would hide a mismatch).
    const IdViewAxis back = addCameraAnim(base, added, 90.0f);
    const float cosTolerance = std::cos(toleranceDegrees * std::numbers::pi_v<float> / 180.0f);
    if (!(dot(back.forward, gameAxis.forward) >= cosTolerance) ||
        !(dot(back.up, gameAxis.up) >= cosTolerance)) {
        return std::nullopt;
    }
    return base;
}

} // namespace evr::xr_math
