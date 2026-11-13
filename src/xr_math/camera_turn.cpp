#include "xr_math/camera_turn.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

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

float largestAngle(const IdAngles& a) {
    return std::max({std::fabs(a.pitch), std::fabs(a.yaw), std::fabs(a.roll)});
}

} // namespace

CameraTurn::CameraTurn(CameraTurnTiming timing) : timing_(timing) {}

void CameraTurn::play(const IdAngles& step) {
    const float limit = timing_.limitDegrees;
    turn_.pitch = std::clamp(turn_.pitch + step.pitch, -limit, limit);
    turn_.yaw = std::clamp(turn_.yaw + step.yaw, -limit, limit);
    turn_.roll = std::clamp(turn_.roll + step.roll, -limit, limit);
    keepLarger(largest_.pitch, turn_.pitch);
    keepLarger(largest_.yaw, turn_.yaw);
    keepLarger(largest_.roll, turn_.roll);
}

void CameraTurn::settle(float after) {
    if (!held_) {
        return;
    }
    const float size = largestAngle(*held_);
    const float share = timing_.cutShare * size;
    if (size > timing_.hugeDegrees || (size > timing_.cutDegrees && before_ < share && after < share)) {
        ++cuts_; // a huge step, or a lone one out of a still camera into a still one: left out
        before_ = 0.0f;
    } else {
        play(*held_);
        before_ = size;
    }
    held_.reset();
}

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
            held_.reset();
            before_ = 0.0f;
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
        settle(largestAngle(step));
        held_ = step;
        return turn_;
    }
    if (last_) {
        settle(0.0f); // the camera's last step: nothing after it
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

IdViewAxis turnYaw(const IdViewAxis& axis, float degrees) {
    const float r = degrees * (std::numbers::pi_v<float> / 180.0f);
    const float c = std::cos(r);
    const float s = std::sin(r);
    const auto turn = [c, s](const Vec3& v) {
        return Vec3{c * v.x - s * v.y, s * v.x + c * v.y, v.z};
    };
    return {turn(axis.forward), turn(axis.left), turn(axis.up)};
}

void CameraHeadingHold::forget() {
    heldViews_ = 0;
    last_.reset();
    lastAngles_.reset();
    lastStep_ = 0.0f;
}

float CameraHeadingHold::update(float cameraYaw,
                                std::optional<float> anglesYaw,
                                double seconds,
                                bool pickup) {
    started_ = false;
    timedOut_ = false;
    if (!std::isfinite(seconds)) {
        forget();
        return 0.0f;
    }
    if (pickup) { // the pickup's clock: a short drop of the flag or of the read does not restart it
        if (pickupSince_ < 0.0 || !(seconds - pickupLast_ <= kPickupGapSeconds)) {
            pickupSince_ = seconds;
            expired_ = false;
        }
        pickupLast_ = seconds;
        if (!expired_ && seconds - pickupSince_ > kMaxPickupSeconds) {
            expired_ = true;
            timedOut_ = true;
        }
    }
    if (!anglesYaw || !std::isfinite(*anglesYaw) || !std::isfinite(cameraYaw)) {
        forget();
        return 0.0f;
    }
    const float offset = normalize180(cameraYaw - *anglesYaw);
    const bool anglesStill =
        lastAngles_ && std::fabs(normalize180(*anglesYaw - *lastAngles_)) <= kAnglesStillDegrees;
    lastAngles_ = anglesYaw;
    const std::optional<float> before = last_;
    const float step = before ? normalize180(offset - *before) : 0.0f;
    const double dt = std::clamp(seconds - lastSeconds_, 0.0, kMaxStepSeconds);
    last_ = offset;
    lastSeconds_ = seconds;
    const float stepBefore = lastStep_;
    lastStep_ = step;
    const bool holding = pickup && !expired_;
    if (heldViews_ > 0) {
        const bool back = std::fabs(normalize180(offset - base_)) <= kStepDegrees;
        // Going on the way it jumped: a motion that started fast, not a flick (which turns back).
        const bool onward = step * startStep_ > 0.0f && std::fabs(step) > kOnwardDegrees;
        if (!holding || !anglesStill || back || onward || heldViews_ >= kMaxHeldViews ||
            seconds - heldSince_ >= kMaxHeldSeconds) {
            heldViews_ = 0; // a flick that came back, or a cut or motion that stays: followed from here
            return 0.0f;
        }
        ++heldViews_;
        return normalize180(base_ - offset);
    }
    const auto rate = static_cast<float>(kStepRate * dt);
    if (holding && anglesStill && before && std::fabs(step) > std::max(kStepDegrees, rate) &&
        std::fabs(step) > kFromStillShare * std::fabs(stepBefore)) {
        base_ = *before;
        startStep_ = step;
        heldViews_ = 1;
        heldSince_ = seconds;
        started_ = true;
        return normalize180(base_ - offset);
    }
    return 0.0f;
}

} // namespace evr::xr_math
