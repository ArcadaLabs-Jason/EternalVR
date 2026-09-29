#include "features/input/wheel_hand.hpp"

#include "common/finite.hpp"
#include "common/vector.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace evr::input {

namespace {

constexpr float kDegrees = std::numbers::pi_v<float> / 180.0f;
constexpr Vec3 kForward{0.0f, 0.0f, -1.0f}; // the aim pose's pointing axis
constexpr Vec3 kRight{1.0f, 0.0f, 0.0f};
constexpr Vec3 kUp{0.0f, 1.0f, 0.0f}; // the room's up
// Below this the start direction counts as straight up or down, where the room's up gives no frame.
constexpr float kMinHorizontal = 0.05f;

// The unit orientation, or nullopt when it is not finite or has no length.
std::optional<Quat> usable(Quat q) {
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) {
        return std::nullopt;
    }
    const float norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(norm > 1e-6f) || !std::isfinite(norm)) {
        return std::nullopt;
    }
    return Quat{q.x / norm, q.y / norm, q.z / norm, q.w / norm};
}

} // namespace

const char* wheelSelectName(WheelSelect select) {
    return select == WheelSelect::Hand ? "hand" : "stick";
}

Axis2 wheelHandPointer(Quat start, Quat current, float fullDegrees) {
    const auto q0 = usable(start);
    const auto q = usable(current);
    if (!q0 || !q) {
        return {};
    }
    const float full =
        finiteInRangeOr(fullDegrees, kMinWheelHandDegrees, kMaxWheelHandDegrees, kDefaultWheelHandDegrees);
    const Vec3 f0 = normalize(rotate(*q0, kForward));
    const Vec3 f = normalize(rotate(*q, kForward));
    // The start's frame without its roll: right is level with the room, up is the room's up tilted with
    // the start's pitch. Straight up or down, the controller's own right stands in for the level one.
    Vec3 right = cross(f0, kUp);
    if (length(right) < kMinHorizontal) {
        const Vec3 own = rotate(*q0, kRight);
        right = own - f0 * dot(own, f0);
    }
    right = normalize(right);
    const Vec3 up = cross(right, f0);
    const float x = dot(f, right);
    const float y = dot(f, up);
    const float sideways = std::hypot(x, y);
    if (!(sideways > 1e-6f)) {
        return {}; // pointing where it started (or straight back)
    }
    const float angle = std::atan2(sideways, dot(f, f0)) / kDegrees;
    const float deflection = std::min(1.0f, angle / full);
    const Axis2 pointer{x / sideways * deflection, y / sideways * deflection};
    return isFinite(pointer) ? pointer : Axis2{};
}

WheelHand::WheelHand(float fullDegrees)
    : fullDegrees_(finiteInRangeOr(
          fullDegrees, kMinWheelHandDegrees, kMaxWheelHandDegrees, kDefaultWheelHandDegrees)) {}

Axis2 WheelHand::update(bool held, bool tracked, Quat aim) {
    if (!held) {
        anchored_ = false;
        return {};
    }
    const auto q = tracked ? usable(aim) : std::nullopt;
    if (!q) {
        return {}; // not tracked: the highlight stays; the reference is kept
    }
    if (!anchored_) {
        anchored_ = true;
        start_ = *q;
        return {};
    }
    return wheelHandPointer(start_, *q, fullDegrees_);
}

} // namespace evr::input
