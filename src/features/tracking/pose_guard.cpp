#include "features/tracking/pose_guard.hpp"

#include <cmath>

namespace evr::tracking {

namespace {

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

bool plausibleHandVelocity(Vec3 velocity) {
    return finite(velocity) && length(velocity) <= kMaxHandMetresPerSecond;
}

bool PoseGuard::reachable(Vec3 from, double fromSeconds, Vec3 to, double toSeconds) const {
    const double seconds = toSeconds > fromSeconds ? toSeconds - fromSeconds : 0.0;
    return length(to - from) <= limits_.minJumpMetres + limits_.metresPerSecond * static_cast<float>(seconds);
}

void PoseGuard::take(Vec3 position, double seconds) {
    last_ = position;
    lastSeconds_ = seconds;
    candidate_.reset();
    holdSince_ = -1.0;
}

void PoseGuard::reset() {
    last_.reset();
    candidate_.reset();
    holdSince_ = -1.0;
}

GuardStep PoseGuard::update(Vec3 position, double seconds) {
    GuardStep step;
    step.position = position;
    if (!finite(position) || !std::isfinite(seconds)) {
        return step;
    }
    if (!last_ || seconds < lastSeconds_ - kClockStepBackSeconds) {
        take(position, seconds); // the first position, or a new clock
        return step;
    }
    const double heldSeconds = holding() ? seconds - holdSince_ : 0.0;
    if (reachable(*last_, lastSeconds_, position, seconds)) {
        step.released = holding();
        step.heldSeconds = heldSeconds;
        take(position, seconds);
        return step;
    }
    if (!holding()) {
        holdSince_ = seconds;
    }
    if (candidate_ && reachable(*candidate_, candidateSeconds_, position, seconds)) {
        candidate_ = position;
        candidateSeconds_ = seconds;
    } else {
        candidate_ = position;
        candidateSince_ = seconds;
        candidateSeconds_ = seconds;
    }
    step.jumpMetres = length(position - *last_);
    step.heldSeconds = seconds - holdSince_;
    if (seconds - candidateSince_ >= kSettleSeconds) {
        step.taken = true;
        take(position, seconds);
        return step;
    }
    step.held = true;
    step.position = *last_;
    step.sinceGoodSeconds = seconds - lastSeconds_;
    return step;
}

} // namespace evr::tracking
