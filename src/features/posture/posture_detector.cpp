#include "features/posture/posture_detector.hpp"

#include "common/finite.hpp"

#include <cmath>

namespace evr::posture {

namespace {

// Head heights outside this range are not a person's; they come from a broken settings file.
constexpr float kMaxThresholdMetres = 3.0f;

PostureThresholds sanitized(PostureThresholds thresholds) {
    const bool valid = finiteInRange(thresholds.seatedBelowMetres, 0.0f, kMaxThresholdMetres) &&
                       finiteInRange(thresholds.standingAboveMetres, 0.0f, kMaxThresholdMetres) &&
                       thresholds.seatedBelowMetres < thresholds.standingAboveMetres;
    return valid ? thresholds : PostureThresholds{};
}

} // namespace

PostureDetector::PostureDetector(PostureThresholds thresholds) : thresholds_(sanitized(thresholds)) {}

Posture PostureDetector::update(std::optional<float> headHeightAboveFloor) {
    // A non-finite height is a tracking glitch, not a measurement.
    if (!headHeightAboveFloor || !std::isfinite(*headHeightAboveFloor)) {
        return current_;
    }
    const float height = *headHeightAboveFloor;

    if (height < thresholds_.seatedBelowMetres) {
        current_ = Posture::Seated;
    } else if (height > thresholds_.standingAboveMetres) {
        current_ = Posture::Standing;
    } else if (current_ == Posture::Unknown) {
        const float midpoint = 0.5f * (thresholds_.seatedBelowMetres + thresholds_.standingAboveMetres);
        current_ = (height < midpoint) ? Posture::Seated : Posture::Standing;
    }
    return current_;
}

Posture effectivePosture(PostureOverride playerOverride, Posture detected) {
    switch (playerOverride) {
    case PostureOverride::Seated:
        return Posture::Seated;
    case PostureOverride::Standing:
        return Posture::Standing;
    case PostureOverride::Auto:
        break;
    }
    return detected;
}

} // namespace evr::posture
