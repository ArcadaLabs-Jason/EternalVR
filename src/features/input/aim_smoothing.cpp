#include "features/input/aim_smoothing.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace evr::input {

namespace {

// The weight a first-order low-pass at `cutoffHz` gives a new sample `dt` seconds after the last.
float smoothingAlpha(float cutoffHz, double dt) {
    const double tau = 1.0 / (2.0 * std::numbers::pi * static_cast<double>(cutoffHz));
    return static_cast<float>(1.0 / (1.0 + tau / dt));
}

float dotQ(Quat a, Quat b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

// From `a` toward `b` by `t` along the shorter arc.
Quat slerp(Quat a, Quat b, float t) {
    float d = dotQ(a, b);
    if (d < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > 0.9995f) {
        return normalize(
            Quat{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z), a.w + t * (b.w - a.w)});
    }
    const float theta = std::acos(std::min(d, 1.0f));
    const float s = std::sin(theta);
    const float wa = std::sin((1.0f - t) * theta) / s;
    const float wb = std::sin(t * theta) / s;
    return normalize(
        Quat{wa * a.x + wb * b.x, wa * a.y + wb * b.y, wa * a.z + wb * b.z, wa * a.w + wb * b.w});
}

// The rotation vector (axis times angle, radians) of a unit quaternion, along the shorter arc.
Vec3 rotationVector(Quat q) {
    if (q.w < 0.0f) {
        q = {-q.x, -q.y, -q.z, -q.w};
    }
    const Vec3 axis{q.x, q.y, q.z};
    const float s = length(axis);
    if (s < 1e-7f) {
        return 2.0f * axis;
    }
    return (2.0f * std::atan2(s, q.w) / s) * axis;
}

} // namespace

std::optional<OneEuroParams> aimSmoothingParams(float strength) {
    if (!std::isfinite(strength) || strength <= 0.0f) {
        return std::nullopt;
    }
    const float s = std::min(strength, 1.0f);
    OneEuroParams p;
    p.minCutoffHz = 8.0f * std::pow(1.0f / 16.0f, s);
    p.beta = 20.0f * std::pow(0.5f, s);
    p.derivativeCutoffHz = 1.0f;
    return p;
}

Quat OneEuroRotation::update(Quat raw, double seconds) {
    raw = normalize(raw);
    const double dt = seconds - lastSeconds_;
    if (!primed_ || !std::isfinite(dt) || dt > kResetSeconds || dt < 0.0) {
        primed_ = true;
        lastSeconds_ = seconds;
        lastRaw_ = raw;
        out_ = raw;
        angularVelocity_ = {};
        return raw;
    }
    if (dt == 0.0) {
        return out_;
    }
    const Vec3 omega = (1.0f / static_cast<float>(dt)) * rotationVector(raw * conjugate(lastRaw_));
    const float ad = smoothingAlpha(params_.derivativeCutoffHz, dt);
    angularVelocity_ = angularVelocity_ + ad * (omega - angularVelocity_);
    const float cutoff = params_.minCutoffHz + params_.beta * length(angularVelocity_);
    out_ = slerp(out_, raw, smoothingAlpha(cutoff, dt));
    lastRaw_ = raw;
    lastSeconds_ = seconds;
    return out_;
}

} // namespace evr::input
