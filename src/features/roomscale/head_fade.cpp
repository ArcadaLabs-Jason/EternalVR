#include "features/roomscale/head_fade.hpp"

#include "common/finite.hpp"

#include <algorithm>
#include <cmath>

namespace evr::roomscale {

namespace {

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

FadeTiming sanitized(FadeTiming t) {
    const FadeTiming defaults;
    t.fullDepthMetres = finiteInRangeOr(t.fullDepthMetres, 0.005f, 1.0f, defaults.fullDepthMetres);
    t.riseSeconds = finiteInRangeOr(t.riseSeconds, 0.0f, 2.0f, defaults.riseSeconds);
    t.fallSeconds = finiteInRangeOr(t.fallSeconds, 0.0f, 5.0f, defaults.fallSeconds);
    return t;
}

} // namespace

ClearanceStep
HeadClearance::update(Vec3 desiredOffset, std::optional<float> hitFraction, float unitsPerMetre) {
    ClearanceStep step;
    if (!finite(desiredOffset)) {
        step.validOffset = lastClear_;
        return step;
    }
    const float scale = finiteInRangeOr(unitsPerMetre, 0.01f, 100.0f, 1.0f);
    if (hitFraction && finiteInRange(*hitFraction, 0.0f, 1.0f)) {
        step.blocked = true;
        step.penetrationMetres = length(desiredOffset) * (1.0f - *hitFraction) / scale;
        // The head where the sphere first touched: clear of geometry for the body where it is now. (The
        // last clear offset is not: when the body walks up to a wall with the head leaned ahead, that offset
        // reaches into the wall.)
        step.validOffset = desiredOffset * *hitFraction;
        lastClear_ = step.validOffset;
        return step;
    }
    lastClear_ = desiredOffset;
    step.validOffset = desiredOffset;
    return step;
}

float fadeTarget(float penetrationMetres, const FadeTiming& timing) {
    const FadeTiming t = sanitized(timing);
    if (!std::isfinite(penetrationMetres) || penetrationMetres <= 0.0f) {
        return 0.0f;
    }
    return std::min(1.0f, penetrationMetres / t.fullDepthMetres);
}

HeadFade::HeadFade(FadeTiming timing) : timing_(sanitized(timing)) {}

float HeadFade::update(float penetrationMetres, double dtSeconds) {
    if (!std::isfinite(dtSeconds) || dtSeconds < 0.0) {
        return value_;
    }
    const float target = fadeTarget(penetrationMetres, timing_);
    const float dt = static_cast<float>(std::min(dtSeconds, 1.0));
    if (target > value_) {
        const float step = timing_.riseSeconds > 0.0f ? dt / timing_.riseSeconds : 1.0f;
        value_ = std::min(target, value_ + step);
    } else if (target < value_) {
        const float step = timing_.fallSeconds > 0.0f ? dt / timing_.fallSeconds : 1.0f;
        value_ = std::max(target, value_ - step);
    }
    return value_;
}

} // namespace evr::roomscale
