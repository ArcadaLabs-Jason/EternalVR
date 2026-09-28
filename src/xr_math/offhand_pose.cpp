#include "xr_math/offhand_pose.hpp"

#include "common/quat.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace evr::xr_math {

namespace {

Vec3 row(const Mat3Rows& m, std::size_t r) {
    return {m[r * 3], m[r * 3 + 1], m[r * 3 + 2]};
}

// The quaternion of a rotation matrix R (R[i][j] = m[3i + j], applied to column vectors) and back. Any
// fixed convention works here: the blend only goes there and back again.
Quat toQuat(const Mat3Rows& m) {
    const float trace = m[0] + m[4] + m[8];
    Quat q;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s, 0.25f * s};
    } else if (m[0] > m[4] && m[0] > m[8]) {
        const float s = std::sqrt(1.0f + m[0] - m[4] - m[8]) * 2.0f;
        q = {0.25f * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s, (m[7] - m[5]) / s};
    } else if (m[4] > m[8]) {
        const float s = std::sqrt(1.0f + m[4] - m[0] - m[8]) * 2.0f;
        q = {(m[1] + m[3]) / s, 0.25f * s, (m[5] + m[7]) / s, (m[2] - m[6]) / s};
    } else {
        const float s = std::sqrt(1.0f + m[8] - m[0] - m[4]) * 2.0f;
        q = {(m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25f * s, (m[3] - m[1]) / s};
    }
    return normalize(q);
}

Mat3Rows fromQuat(Quat q) {
    const Vec3 x = rotate(q, {1.0f, 0.0f, 0.0f});
    const Vec3 y = rotate(q, {0.0f, 1.0f, 0.0f});
    const Vec3 z = rotate(q, {0.0f, 0.0f, 1.0f});
    // Columns are the rotated basis vectors.
    return {x.x, y.x, z.x, x.y, y.y, z.y, x.z, y.z, z.z};
}

Quat slerp(Quat a, Quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > 0.9995f) {
        return normalize(
            Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    }
    const float theta = std::acos(std::clamp(d, -1.0f, 1.0f));
    const float s = std::sin(theta);
    const float wa = std::sin((1.0f - t) * theta) / s;
    const float wb = std::sin(t * theta) / s;
    return normalize(
        Quat{a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb});
}

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool orthonormal(const Mat3Rows& m, float tolerance) {
    for (const float v : m) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    const Vec3 r0 = row(m, 0);
    const Vec3 r1 = row(m, 1);
    const Vec3 r2 = row(m, 2);
    return std::fabs(dot(r0, r0) - 1.0f) < tolerance && std::fabs(dot(r1, r1) - 1.0f) < tolerance &&
           std::fabs(dot(r2, r2) - 1.0f) < tolerance && std::fabs(dot(r0, r1)) < tolerance &&
           std::fabs(dot(r0, r2)) < tolerance && std::fabs(dot(r1, r2)) < tolerance &&
           dot(cross(r0, r1), r2) > 0.0f; // a rotation, not a reflection
}

} // namespace

Mat3Rows toRows(const IdViewAxis& a) {
    return {a.forward.x, a.forward.y, a.forward.z, a.left.x, a.left.y, a.left.z, a.up.x, a.up.y, a.up.z};
}

IdViewAxis fromRows(const Mat3Rows& m) {
    return {row(m, 0), row(m, 1), row(m, 2)};
}

Mat3Rows multiply(const Mat3Rows& a, const Mat3Rows& b) {
    Mat3Rows out{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            out[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
        }
    }
    return out;
}

Mat3Rows transpose(const Mat3Rows& m) {
    return {m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]};
}

ModelPose inModelSpace(const EyeRelativePose& model, const EyeRelativePose& target) {
    const IdViewAxis& m = model.axis;
    const auto local = [&m](Vec3 v) {
        return Vec3{dot(v, m.forward), dot(v, m.left), dot(v, m.up)};
    };
    return {local(target.offset - model.offset),
            {local(target.axis.forward), local(target.axis.left), local(target.axis.up)}};
}

JointMod jointModToward(const ModelPose& animated, const ModelPose& target) {
    return {target.position - animated.position,
            multiply(transpose(toRows(animated.axis)), toRows(target.axis))};
}

ModelPose applyJointMod(const ModelPose& animated, const JointMod& mod) {
    return {animated.position + mod.translation, fromRows(multiply(toRows(animated.axis), mod.rotation))};
}

JointMod blendJointMods(const JointMod& game, const JointMod& ours, float weight) {
    const float w = std::isfinite(weight) ? std::clamp(weight, 0.0f, 1.0f) : 0.0f;
    if (w <= 0.0f) {
        return game;
    }
    if (w >= 1.0f) {
        return ours;
    }
    JointMod out;
    out.translation = game.translation + (ours.translation - game.translation) * w;
    out.rotation = fromQuat(slerp(toQuat(game.rotation), toQuat(ours.rotation), w));
    return out;
}

bool plausibleJointMod(const JointMod& mod, float maxLength, float tolerance) {
    return finite(mod.translation) && length(mod.translation) <= maxLength &&
           orthonormal(mod.rotation, tolerance);
}

bool plausibleAnimatedPose(const ModelPose& pose, float tolerance) {
    const Mat3Rows m = toRows(pose.axis);
    if (!finite(pose.position) || !orthonormal(m, tolerance)) {
        return false;
    }
    const Mat3Rows identity{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    return !(m == identity && pose.position == Vec3{});
}

} // namespace evr::xr_math
