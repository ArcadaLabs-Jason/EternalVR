#include "features/input/wheel_slowdown.hpp"

#include <cmath>

namespace evr::input {

SlowdownAction WheelSlowdown::update(bool restWheel, bool otherWheel, float dtSeconds) {
    if (!holds_) {
        return SlowdownAction::None;
    }
    const float dt = std::isfinite(dtSeconds) && dtSeconds > 0.0f ? dtSeconds : 0.0f;
    if (restWheel) {
        sinceRelease_ = 0.0f;
        if (held_) {
            return SlowdownAction::None;
        }
        held_ = true;
        return SlowdownAction::Hold;
    }
    if (!held_) {
        return SlowdownAction::None;
    }
    sinceRelease_ += dt;
    if (otherWheel || sinceRelease_ >= kSlowdownRestoreSeconds) {
        return reset();
    }
    return SlowdownAction::None;
}

SlowdownAction WheelSlowdown::reset() {
    sinceRelease_ = 0.0f;
    if (!held_) {
        return SlowdownAction::None;
    }
    held_ = false;
    return SlowdownAction::Restore;
}

} // namespace evr::input
