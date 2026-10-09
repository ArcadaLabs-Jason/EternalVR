#include "features/tracking/fov_watch.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace evr::tracking {

namespace {

constexpr float kDegrees = 57.29578f;

bool finite(const xr_math::Fov& f) {
    return std::isfinite(f.angleLeft) && std::isfinite(f.angleRight) && std::isfinite(f.angleUp) &&
           std::isfinite(f.angleDown);
}

// The wider of two half-angles over the narrower one (both positive).
float lopsided(float a, float b) {
    return std::max(a, b) / std::min(a, b);
}

FovProblem eyeProblem(const xr_math::Fov& f) {
    if (!finite(f)) {
        return FovProblem::NotFinite;
    }
    // Each side as a positive angle from the centre, in degrees.
    const float left = -f.angleLeft * kDegrees;
    const float right = f.angleRight * kDegrees;
    const float up = f.angleUp * kDegrees;
    const float down = -f.angleDown * kDegrees;
    for (const float side : {left, right, up, down}) {
        if (side < kMinHalfDegrees || side > kMaxHalfDegrees) {
            return FovProblem::Edge;
        }
    }
    if (left + right < kMinAcrossDegrees || up + down < kMinUpDownDegrees) {
        return FovProblem::Narrow;
    }
    if (lopsided(left, right) > kMaxLopsided || lopsided(up, down) > kMaxLopsided) {
        return FovProblem::Lopsided;
    }
    return FovProblem::None;
}

bool spansAgree(float a, float b) {
    return std::fabs(a - b) <= kMaxEyeMismatch * std::max(a, b);
}

} // namespace

FovProblem fovProblem(const EyeFovs& eyes) {
    for (const xr_math::Fov& f : eyes) {
        if (const FovProblem problem = eyeProblem(f); problem != FovProblem::None) {
            return problem;
        }
    }
    const xr_math::Fov& l = eyes[0];
    const xr_math::Fov& r = eyes[1];
    if (!spansAgree(l.angleRight - l.angleLeft, r.angleRight - r.angleLeft) ||
        !spansAgree(l.angleUp - l.angleDown, r.angleUp - r.angleDown)) {
        return FovProblem::EyesDiffer;
    }
    return FovProblem::None;
}

const char* fovProblemText(FovProblem problem) {
    switch (problem) {
    case FovProblem::None:
        return "plausible";
    case FovProblem::NotFinite:
        return "an angle is not a number";
    case FovProblem::Narrow:
        return "an eye is too narrow";
    case FovProblem::Edge:
        return "a side is too close to the centre or too far from it";
    case FovProblem::Lopsided:
        return "an eye is lopsided";
    case FovProblem::EyesDiffer:
        return "the eyes differ";
    }
    return "?";
}

bool sameFovs(const EyeFovs& a, const EyeFovs& b, float degrees) {
    const float radians = degrees / kDegrees;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!(std::fabs(a[i].angleLeft - b[i].angleLeft) <= radians &&
              std::fabs(a[i].angleRight - b[i].angleRight) <= radians &&
              std::fabs(a[i].angleUp - b[i].angleUp) <= radians &&
              std::fabs(a[i].angleDown - b[i].angleDown) <= radians)) {
            return false;
        }
    }
    return true;
}

FovWatch::Outcome FovWatch::onRead(const EyeFovs& eyes, double seconds, bool check) {
    // Unchecked, a read that is not even finite is still never taken.
    problem_ = check                                ? fovProblem(eyes)
               : finite(eyes[0]) && finite(eyes[1]) ? FovProblem::None
                                                    : FovProblem::NotFinite;
    Outcome outcome = Outcome::Unchanged;
    if (problem_ != FovProblem::None) {
        ++implausible_;
        ++implausibleRun_;
        outcome = Outcome::Implausible;
    } else if (!taken_) {
        taken_ = eyes;
        outcome = Outcome::First;
    } else if (sameFovs(*taken_, eyes, kSameDegrees)) {
        candidate_.reset(); // back to the FOV taken: a change that did not hold
        candidateReads_ = 0;
    } else {
        if (candidate_ && sameFovs(*candidate_, eyes, kSameDegrees)) {
            ++candidateReads_;
        } else {
            candidate_ = eyes;
            candidateReads_ = 1;
        }
        outcome = Outcome::Waiting;
        if (candidateReads_ >= kStableReads) {
            taken_ = eyes;
            candidate_.reset();
            candidateReads_ = 0;
            ++changes_;
            outcome = Outcome::Changed;
        }
    }
    if (problem_ == FovProblem::None) {
        implausibleRun_ = 0;
    }
    const bool soon = (!taken_ || candidate_) && implausibleRun_ < kRetryReads;
    next_ = seconds + (soon ? kRetrySeconds : kReadSeconds);
    return outcome;
}

} // namespace evr::tracking
