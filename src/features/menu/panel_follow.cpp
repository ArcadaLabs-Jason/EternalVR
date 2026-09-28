#include "features/menu/panel_follow.hpp"

#include "common/quat.hpp"

#include <algorithm>
#include <cmath>

namespace evr::menu {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kMinLength = 1e-4f;

} // namespace

float horizontalAngleTo(const Pose& head, Vec3 target) {
    const Vec3 forward = rotate(head.orientation, Vec3{0.0f, 0.0f, -1.0f});
    const Vec3 to = target - head.position;
    const float forwardLength = std::hypot(forward.x, forward.z);
    const float toLength = std::hypot(to.x, to.z);
    if (forwardLength < kMinLength || toLength < kMinLength) {
        return 0.0f;
    }
    const float cosine = (forward.x * to.x + forward.z * to.z) / (forwardLength * toLength);
    return std::acos(std::clamp(cosine, -1.0f, 1.0f)) * 180.0f / kPi;
}

PanelFollow::PanelFollow(PanelFollowTuning tuning) : tuning_(tuning) {
    if (!std::isfinite(tuning_.awayDegrees) || tuning_.awayDegrees <= 0.0f || tuning_.awayDegrees >= 180.0f) {
        tuning_.awayDegrees = PanelFollowTuning{}.awayDegrees;
    }
    if (!std::isfinite(tuning_.awaySeconds) || tuning_.awaySeconds < 0.0) {
        tuning_.awaySeconds = PanelFollowTuning{}.awaySeconds;
    }
}

bool PanelFollow::update(float angleDegrees, double seconds) {
    if (!std::isfinite(angleDegrees) || angleDegrees <= tuning_.awayDegrees) {
        awaySince_.reset();
        return false;
    }
    if (!awaySince_) {
        awaySince_ = seconds;
    }
    if (seconds - *awaySince_ < tuning_.awaySeconds) {
        return false;
    }
    awaySince_.reset();
    return true;
}

} // namespace evr::menu
