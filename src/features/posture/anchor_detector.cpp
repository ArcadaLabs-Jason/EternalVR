#include "features/posture/anchor_detector.hpp"

#include <algorithm>
#include <cmath>

namespace evr::posture {

namespace {

bool positive(float value) {
    return std::isfinite(value) && value > 0.0f;
}

AnchorThresholds sanitized(AnchorThresholds t) {
    const bool valid = positive(t.windowSeconds) && positive(t.stableMetres) && positive(t.wornMetres) &&
                       positive(t.wornRadians) && t.stableMetres > t.wornMetres;
    return valid ? t : AnchorThresholds{};
}

bool finitePose(const Pose& p) {
    return std::isfinite(p.position.x) && std::isfinite(p.position.y) && std::isfinite(p.position.z) &&
           std::isfinite(p.orientation.x) && std::isfinite(p.orientation.y) &&
           std::isfinite(p.orientation.z) && std::isfinite(p.orientation.w);
}

// The angle between two orientations, in radians.
float angleBetween(Quat a, Quat b) {
    const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.0f * std::acos(std::min(1.0f, d));
}

} // namespace

AnchorDetector::AnchorDetector(AnchorThresholds thresholds) : thresholds_(sanitized(thresholds)) {}

AnchorDetector::Window AnchorDetector::window() const {
    Window w;
    w.samples = samples_.size();
    if (samples_.empty()) {
        return w;
    }
    w.seconds = samples_.back().seconds - samples_.front().seconds;
    Vec3 lo = samples_.front().pose.position;
    Vec3 hi = lo;
    const Quat newest = normalize(samples_.back().pose.orientation);
    for (const HeadSample& s : samples_) {
        const Vec3 p = s.pose.position;
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        w.rotationSpan = std::max(w.rotationSpan, angleBetween(newest, normalize(s.pose.orientation)));
    }
    w.positionSpan = length(hi - lo);
    return w;
}

std::optional<Pose> AnchorDetector::update(const HeadSample& sample) {
    if (anchored_) {
        return std::nullopt;
    }
    const bool counts = sample.tracked && sample.focused && sample.userPresent.value_or(true) &&
                        std::isfinite(sample.seconds) && finitePose(sample.pose);
    if (!counts || (!samples_.empty() && sample.seconds < samples_.back().seconds)) {
        samples_.clear();
        return std::nullopt;
    }
    samples_.push_back(sample);
    // Keep exactly one window: drop the oldest while the rest still spans the window.
    const double window = thresholds_.windowSeconds;
    while (samples_.size() > 2 && sample.seconds - samples_[1].seconds >= window) {
        samples_.erase(samples_.begin());
    }
    const Window w = this->window();
    if (w.seconds < window) {
        return std::nullopt;
    }
    const bool stable = w.positionSpan < thresholds_.stableMetres;
    const bool worn = w.positionSpan > thresholds_.wornMetres || w.rotationSpan > thresholds_.wornRadians;
    if (!stable || !worn) {
        return std::nullopt;
    }
    anchored_ = true;
    samples_.clear();
    return sample.pose;
}

void AnchorDetector::reset() {
    samples_.clear();
    anchored_ = false;
}

} // namespace evr::posture
