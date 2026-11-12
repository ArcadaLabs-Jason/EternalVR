#include "xr_math/camera_turn.hpp"

#include <algorithm>
#include <cmath>

namespace evr::xr_math {

namespace {

bool finite(const IdAngles& a) {
    return std::isfinite(a.pitch) && std::isfinite(a.yaw) && std::isfinite(a.roll);
}

void keepLarger(float& keep, float value) {
    if (std::fabs(value) > std::fabs(keep)) {
        keep = value;
    }
}

} // namespace

CameraTurn::CameraTurn(CameraTurnTiming timing) : timing_(timing) {}

std::optional<IdAngles> CameraTurn::update(bool follow, const IdViewAxis& camera, double seconds) {
    if (follow) {
        const IdAngles now = anglesFromAxis(camera);
        if (!last_) {
            // Followed from here: from none, or from what a release has left (turn_ holds its last value).
            if (!releasing_) {
                turn_ = {};
                cuts_ = 0;
                largest_ = {};
            }
            releasing_ = false;
            if (finite(now)) {
                last_ = now;
            }
            return turn_;
        }
        if (!finite(now)) {
            return turn_;
        }
        const IdAngles step{normalize180(now.pitch - last_->pitch), normalize180(now.yaw - last_->yaw),
                            normalize180(now.roll - last_->roll)};
        last_ = now;
        if (std::max({std::fabs(step.pitch), std::fabs(step.yaw), std::fabs(step.roll)}) >
            timing_.cutDegrees) {
            ++cuts_;
            return turn_;
        }
        const float limit = timing_.limitDegrees;
        turn_.pitch = std::clamp(turn_.pitch + step.pitch, -limit, limit);
        turn_.yaw = std::clamp(turn_.yaw + step.yaw, -limit, limit);
        turn_.roll = std::clamp(turn_.roll + step.roll, -limit, limit);
        keepLarger(largest_.pitch, turn_.pitch);
        keepLarger(largest_.yaw, turn_.yaw);
        keepLarger(largest_.roll, turn_.roll);
        return turn_;
    }
    if (last_) {
        last_.reset();
        releasing_ = true;
        releaseFrom_ = turn_;
        releaseStart_ = seconds;
    }
    if (!releasing_) {
        return std::nullopt;
    }
    const double t = (seconds - releaseStart_) / timing_.releaseSeconds;
    if (!std::isfinite(t) || t >= 1.0 || timing_.releaseSeconds <= 0.0) {
        releasing_ = false;
        turn_ = {};
        return std::nullopt;
    }
    const auto u = static_cast<float>(std::max(t, 0.0));
    const float weight = 1.0f - u * u * (3.0f - 2.0f * u);
    turn_ = {releaseFrom_.pitch * weight, releaseFrom_.yaw * weight, releaseFrom_.roll * weight};
    return turn_;
}

IdAngles turnOnBody(const IdAngles& turn, bool bodyOwnHeading) {
    return bodyOwnHeading ? turn : IdAngles{turn.pitch, 0.0f, turn.roll};
}

} // namespace evr::xr_math
