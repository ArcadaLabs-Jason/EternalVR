#include "features/arm/arm_twist.hpp"

#include <algorithm>
#include <cmath>

namespace evr::arm {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTurn = 2.0f * kPi;

bool finite(float v) {
    return std::isfinite(v);
}

} // namespace

bool neutralFrame(Vec3 bone, Vec3 normal, float side, IdViewAxis& out) {
    const float bl = length(bone);
    if (!(bl > 1e-6f) || !finite(bl) || !finite(normal.x) || !finite(normal.y) || !finite(normal.z)) {
        return false;
    }
    const Vec3 y = bone * ((side < 0.0f ? -1.0f : 1.0f) / bl);
    const Vec3 across = normal - y * dot(normal, y);
    const float al = length(across);
    if (!(al > 1e-4f)) {
        return false;
    }
    const Vec3 z = across * (-1.0f / al);
    out = {cross(y, z), y, z};
    return true;
}

float unwrapTwist(float raw, std::optional<float> last, const TwistLimits& limits) {
    if (!finite(raw)) {
        return limits.centre;
    }
    float t = raw;
    if (!last || !finite(*last)) {
        while (t > limits.centre + kPi) {
            t -= kTurn;
        }
        while (t <= limits.centre - kPi) {
            t += kTurn;
        }
        return t;
    }
    t = raw + kTurn * std::round((*last - raw) / kTurn);
    const float reach = kPi + limits.hysteresis;
    while (t > limits.centre + reach) {
        t -= kTurn;
    }
    while (t < limits.centre - reach) {
        t += kTurn;
    }
    return t;
}

TwistSplit splitTwist(float twist, const TwistLimits& limits) {
    TwistSplit out;
    if (!finite(twist)) {
        return out;
    }
    out.forearm = limits.centre + std::clamp(twist - limits.centre, -limits.forearm, limits.forearm);
    out.upper = std::clamp(twist - out.forearm, -limits.upper, limits.upper);
    out.wrist = twist - out.forearm - out.upper;
    return out;
}

SwingResult limitSwing(const IdViewAxis& neutral, const IdViewAxis& hand, const TwistLimits& limits) {
    SwingResult out;
    out.hand = hand;
    // The swing turns the forearm's line (the neutral y) onto the hand's y; the twist about it is left.
    const Vec3 along = neutral.left;
    const float c = std::clamp(dot(along, hand.left), -1.0f, 1.0f);
    const float angle = std::acos(c);
    if (!(angle > 1e-5f)) {
        return out;
    }
    Vec3 axis = cross(along, hand.left);
    const float al = length(axis);
    // Half a turn has no one axis; the flexion axis is as good as any.
    axis = al > 1e-6f ? axis * (1.0f / al) : neutral.forward;
    const Vec3 local = toLocal(neutral, axis);
    out.flex = angle * local.x;
    out.deviation = angle * local.z;
    const float fx = out.flex / std::max(limits.flex, 1e-4f);
    const float dz = out.deviation / std::max(limits.deviation, 1e-4f);
    const float r = std::sqrt(fx * fx + dz * dz);
    if (!(r > 1.0f)) {
        return out;
    }
    out.clamped = true;
    out.hand = rotateAbout(hand, axis, angle / r - angle);
    return out;
}

} // namespace evr::arm
