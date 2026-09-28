#include "features/posture/posture_tracker.hpp"

#include "common/finite.hpp"

#include <cmath>

namespace evr::posture {

namespace {

PostureTrackerSettings sanitized(PostureTrackerSettings s) {
    const bool valid =
        finiteInRange(s.standingAboveMetres, 0.5f, 2.5f) && finiteInRange(s.seatedBelowMetres, 0.5f, 2.5f) &&
        s.seatedBelowMetres < s.standingAboveMetres && finiteInRange(s.minChangeMetres, 0.05f, 1.0f) &&
        finiteInRange(s.standingSeconds, 0.1f, 10.0f) && finiteInRange(s.seatedSeconds, 0.1f, 10.0f);
    return valid ? s : PostureTrackerSettings{};
}

} // namespace

PostureTracker::PostureTracker(PostureTrackerSettings settings) : settings_(sanitized(settings)) {}

void PostureTracker::reset(Posture current, std::optional<float> anchorHeightMetres) {
    const bool known =
        current != Posture::Unknown && anchorHeightMetres && std::isfinite(*anchorHeightMetres);
    current_ = known ? current : Posture::Unknown;
    reference_ = known ? *anchorHeightMetres : 0.0f;
    candidate_ = Posture::Unknown;
    changing_ = false;
}

std::optional<PostureChange> PostureTracker::update(std::optional<float> headAboveFloorMetres,
                                                    double seconds) {
    if (current_ == Posture::Unknown || !std::isfinite(seconds)) {
        return std::nullopt;
    }
    if (candidate_ != Posture::Unknown && seconds < last_) {
        since_ = seconds; // time went backwards: the dwell starts again
    }
    last_ = seconds;
    if (!headAboveFloorMetres || !std::isfinite(*headAboveFloorMetres)) {
        return std::nullopt;
    }
    const float height = *headAboveFloorMetres;
    const PostureTrackerSettings& s = settings_;

    Posture target = Posture::Unknown;
    if (current_ == Posture::Seated) {
        changing_ = height > reference_ + 0.5f * s.minChangeMetres;
        if (height > s.standingAboveMetres && height >= reference_ + s.minChangeMetres) {
            target = Posture::Standing;
        }
    } else {
        changing_ = height < reference_ - 0.5f * s.minChangeMetres;
        if (height < s.seatedBelowMetres && height <= reference_ - s.minChangeMetres) {
            target = Posture::Seated;
        }
    }
    if (target == Posture::Unknown) {
        candidate_ = Posture::Unknown;
        return std::nullopt;
    }
    if (candidate_ != target) {
        candidate_ = target;
        since_ = seconds;
    }
    const double held = seconds - since_;
    const float dwell = target == Posture::Standing ? s.standingSeconds : s.seatedSeconds;
    if (held < static_cast<double>(dwell)) {
        return std::nullopt;
    }
    PostureChange change;
    change.from = current_;
    change.to = target;
    change.heightMetres = height;
    change.heldSeconds = static_cast<float>(held);
    current_ = target;
    reference_ = height;
    candidate_ = Posture::Unknown;
    changing_ = false;
    return change;
}

} // namespace evr::posture
