#include "features/arm/arm_frames.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace evr::arm {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
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

} // namespace

Vec3 toLocal(const IdViewAxis& frame, Vec3 v) {
    return {dot(v, frame.forward), dot(v, frame.left), dot(v, frame.up)};
}

Vec3 fromLocal(const IdViewAxis& frame, Vec3 local) {
    return frame.forward * local.x + frame.left * local.y + frame.up * local.z;
}

ModelPose relative(const ModelPose& parent, const ModelPose& child) {
    const IdViewAxis& p = parent.axis;
    return {toLocal(p, child.position - parent.position),
            {toLocal(p, child.axis.forward), toLocal(p, child.axis.left), toLocal(p, child.axis.up)}};
}

ModelPose compose(const ModelPose& parent, const ModelPose& local) {
    const IdViewAxis& p = parent.axis;
    return {parent.position + fromLocal(p, local.position),
            {fromLocal(p, local.axis.forward), fromLocal(p, local.axis.left), fromLocal(p, local.axis.up)}};
}

ModelPose parentFor(const ModelPose& child, const ModelPose& local) {
    // child.axis rows = local.axis rows (parent coordinates) times the parent's rows, so each parent row is
    // the child's rows weighted by the matching column of local.axis (an orthonormal matrix).
    const IdViewAxis& l = local.axis;
    const IdViewAxis& c = child.axis;
    ModelPose parent;
    parent.axis.forward = c.forward * l.forward.x + c.left * l.left.x + c.up * l.up.x;
    parent.axis.left = c.forward * l.forward.y + c.left * l.left.y + c.up * l.up.y;
    parent.axis.up = c.forward * l.forward.z + c.left * l.left.z + c.up * l.up.z;
    parent.position = child.position - fromLocal(parent.axis, local.position);
    return parent;
}

bool frameFrom(Vec3 direction, Vec3 normal, IdViewAxis& out) {
    const float dl = length(direction);
    if (!(dl > 1e-6f) || !finite(direction) || !finite(normal)) {
        return false;
    }
    const Vec3 f = direction * (1.0f / dl);
    const Vec3 side = normal - f * dot(normal, f);
    const float sl = length(side);
    if (!(sl > 1e-4f)) {
        return false;
    }
    const Vec3 l = side * (1.0f / sl);
    out = {f, l, cross(f, l)};
    return true;
}

Vec3 rotateAbout(Vec3 v, Vec3 axis, float radians) {
    return rotate(Quat::fromAxisAngle(axis, radians), v);
}

IdViewAxis rotateAbout(const IdViewAxis& frame, Vec3 axis, float radians) {
    const Quat q = Quat::fromAxisAngle(axis, radians);
    return {rotate(q, frame.forward), rotate(q, frame.left), rotate(q, frame.up)};
}

Quat toQuat(const IdViewAxis& a) {
    // R's columns are the rows of the axis: R[i][j] = row j, component i.
    const Vec3& f = a.forward;
    const Vec3& l = a.left;
    const Vec3& u = a.up;
    const float trace = f.x + l.y + u.z;
    Quat q;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(l.z - u.y) / s, (u.x - f.z) / s, (f.y - l.x) / s, 0.25f * s};
    } else if (f.x > l.y && f.x > u.z) {
        const float s = std::sqrt(1.0f + f.x - l.y - u.z) * 2.0f;
        q = {0.25f * s, (l.x + f.y) / s, (u.x + f.z) / s, (l.z - u.y) / s};
    } else if (l.y > u.z) {
        const float s = std::sqrt(1.0f + l.y - f.x - u.z) * 2.0f;
        q = {(l.x + f.y) / s, 0.25f * s, (u.y + l.z) / s, (u.x - f.z) / s};
    } else {
        const float s = std::sqrt(1.0f + u.z - f.x - l.y) * 2.0f;
        q = {(u.x + f.z) / s, (u.y + l.z) / s, 0.25f * s, (f.y - l.x) / s};
    }
    return normalize(q);
}

IdViewAxis fromQuat(Quat q) {
    return {rotate(q, {1.0f, 0.0f, 0.0f}), rotate(q, {0.0f, 1.0f, 0.0f}), rotate(q, {0.0f, 0.0f, 1.0f})};
}

Quat rotationBetween(const IdViewAxis& from, const IdViewAxis& to) {
    return normalize(toQuat(to) * conjugate(toQuat(from)));
}

float twistAbout(const IdViewAxis& from, const IdViewAxis& to, Vec3 axis) {
    const Quat q = rotationBetween(from, to);
    const Vec3 a = normalize(axis);
    float angle = 2.0f * std::atan2(dot(Vec3{q.x, q.y, q.z}, a), q.w);
    if (angle > kPi) {
        angle -= 2.0f * kPi;
    } else if (angle <= -kPi) {
        angle += 2.0f * kPi;
    }
    return std::isfinite(angle) ? angle : 0.0f;
}

ModelPose blendPose(const ModelPose& a, const ModelPose& b, float weight) {
    const float w = std::isfinite(weight) ? std::clamp(weight, 0.0f, 1.0f) : 0.0f;
    if (w <= 0.0f) {
        return a;
    }
    if (w >= 1.0f) {
        return b;
    }
    return {a.position + (b.position - a.position) * w, fromQuat(slerp(toQuat(a.axis), toQuat(b.axis), w))};
}

bool plausiblePose(const ModelPose& pose, float tolerance) {
    const IdViewAxis& a = pose.axis;
    if (!finite(pose.position) || !finite(a.forward) || !finite(a.left) || !finite(a.up)) {
        return false;
    }
    return std::fabs(dot(a.forward, a.forward) - 1.0f) < tolerance &&
           std::fabs(dot(a.left, a.left) - 1.0f) < tolerance &&
           std::fabs(dot(a.up, a.up) - 1.0f) < tolerance && std::fabs(dot(a.forward, a.left)) < tolerance &&
           std::fabs(dot(a.forward, a.up)) < tolerance && std::fabs(dot(a.left, a.up)) < tolerance &&
           dot(cross(a.forward, a.left), a.up) > 0.0f;
}

} // namespace evr::arm
