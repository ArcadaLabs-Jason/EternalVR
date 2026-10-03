#include "features/arm/two_bone_ik.hpp"

#include <algorithm>
#include <cmath>

namespace evr::arm {

namespace {

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Some unit vector perpendicular to the unit vector `v`.
Vec3 perpendicular(Vec3 v) {
    const Vec3 other = std::fabs(v.z) < 0.9f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
    return normalize(cross(other, v));
}

} // namespace

std::optional<TwoBoneSolution> solveTwoBone(const TwoBoneInput& in) {
    const float a = in.upper;
    const float b = in.lower;
    if (!std::isfinite(a) || !std::isfinite(b) || !(a > 1e-6f) || !(b > 1e-6f) || !finite(in.root) ||
        !finite(in.target) || !finite(in.pole)) {
        return std::nullopt;
    }
    const float full = a + b;
    const float maxReach = full * kMaxReachFraction;
    const float minReach = std::max(std::fabs(a - b), full * kMinReachFraction);

    const Vec3 toTarget = in.target - in.root;
    const float d = length(toTarget);
    Vec3 dir;
    if (d > 1e-6f * full) {
        dir = toTarget * (1.0f / d);
    } else {
        // The target on the shoulder: reach out across the pole, any way will do.
        const Vec3 pole = normalize(in.pole);
        dir = length(pole) > 0.5f ? perpendicular(pole) : Vec3{1.0f, 0.0f, 0.0f};
    }

    TwoBoneSolution out;
    out.reach = d / full;
    out.clamped = d > maxReach || d < minReach;
    const float reach = std::clamp(d, minReach, maxReach);
    out.end = in.root + dir * reach;

    // Law of cosines at the shoulder.
    const float cosShoulder = std::clamp((a * a + reach * reach - b * b) / (2.0f * a * reach), -1.0f, 1.0f);
    const float along = a * cosShoulder;
    const float across = a * std::sqrt(std::max(0.0f, 1.0f - cosShoulder * cosShoulder));

    Vec3 bend = in.pole - dir * dot(in.pole, dir);
    bend = length(bend) > 1e-4f * std::max(1.0f, length(in.pole)) ? normalize(bend) : perpendicular(dir);
    out.bend = bend;
    out.joint = in.root + dir * along + bend * across;
    if (!finite(out.joint) || !finite(out.end)) {
        return std::nullopt;
    }
    return out;
}

Vec3 rootWithinReach(Vec3 root, Vec3 target, float upper, float lower) {
    if (!std::isfinite(upper) || !std::isfinite(lower) || !(upper > 1e-6f) || !(lower > 1e-6f) ||
        !finite(root) || !finite(target)) {
        return root;
    }
    const float full = upper + lower;
    // A little inside the limits, so rounding never clamps.
    const float maxReach = full * kMaxReachFraction * 0.999f;
    const float minReach = std::max(std::fabs(upper - lower), full * kMinReachFraction) * 1.001f;
    const Vec3 fromTarget = root - target;
    const float d = length(fromTarget);
    if (!(d > 1e-6f * full) || (d <= maxReach && d >= minReach)) {
        return root;
    }
    return target + fromTarget * (std::clamp(d, minReach, maxReach) / d);
}

} // namespace evr::arm
