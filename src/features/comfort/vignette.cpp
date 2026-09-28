#include "features/comfort/vignette.hpp"

#include "common/finite.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace evr::comfort {

namespace {

constexpr float kDegrees = 57.2957795f;

VignetteTiming sanitized(VignetteTiming t) {
    const VignetteTiming d;
    t.turnStartDegreesPerSecond =
        finiteInRangeOr(t.turnStartDegreesPerSecond, 0.0f, 1000.0f, d.turnStartDegreesPerSecond);
    t.turnFullDegreesPerSecond =
        finiteInRangeOr(t.turnFullDegreesPerSecond, 0.0f, 1000.0f, d.turnFullDegreesPerSecond);
    if (!(t.turnFullDegreesPerSecond > t.turnStartDegreesPerSecond)) {
        t.turnStartDegreesPerSecond = d.turnStartDegreesPerSecond;
        t.turnFullDegreesPerSecond = d.turnFullDegreesPerSecond;
    }
    t.moveStart = finiteInRangeOr(t.moveStart, 0.0f, 1.0f, d.moveStart);
    t.moveFull = finiteInRangeOr(t.moveFull, 0.0f, 1.0f, d.moveFull);
    if (!(t.moveFull > t.moveStart)) {
        t.moveStart = d.moveStart;
        t.moveFull = d.moveFull;
    }
    t.riseSeconds = finiteInRangeOr(t.riseSeconds, 0.0f, 5.0f, d.riseSeconds);
    t.fallSeconds = finiteInRangeOr(t.fallSeconds, 0.0f, 5.0f, d.fallSeconds);
    return t;
}

// 0 at `start` and below, 1 at `full` and above, linear between; 0 for a non-finite value.
float ramp(float value, float start, float full) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return std::clamp((value - start) / (full - start), 0.0f, 1.0f);
}

float smoothstep(float edge0, float edge1, float x) {
    if (!(edge1 > edge0)) {
        return x < edge0 ? 0.0f : 1.0f;
    }
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

float vignetteTarget(const VignetteMotion& motion, const VignetteTiming& timing) {
    if (motion.gameMotion) {
        return 1.0f;
    }
    const VignetteTiming t = sanitized(timing);
    const float turn =
        ramp(std::fabs(motion.turnDegreesPerSecond), t.turnStartDegreesPerSecond, t.turnFullDegreesPerSecond);
    const float move = ramp(std::fabs(motion.moveMagnitude), t.moveStart, t.moveFull);
    return std::max(turn, move);
}

VignettePolicy::VignettePolicy(VignetteTiming timing) : timing_(sanitized(timing)) {}

float VignettePolicy::update(const VignetteMotion& motion, double dtSeconds) {
    if (!std::isfinite(dtSeconds) || dtSeconds < 0.0) {
        return value_;
    }
    const float target = vignetteTarget(motion, timing_);
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

VignetteShape vignetteShape(float amount, const VignetteLook& look) {
    const float a = std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 0.0f;
    VignetteShape shape;
    shape.clearDegrees = look.startClearDegrees + (look.fullClearDegrees - look.startClearDegrees) * a;
    shape.darkDegrees = shape.clearDegrees + std::max(0.0f, look.featherDegrees);
    shape.opacity = std::clamp(look.opacity, 0.0f, 1.0f) * a;
    return shape;
}

int vignetteLevel(float amount, int levels) {
    if (levels <= 0 || !std::isfinite(amount)) {
        return 0;
    }
    const float scaled = std::clamp(amount, 0.0f, 1.0f) * static_cast<float>(levels);
    return std::clamp(static_cast<int>(std::lround(scaled)), 0, levels);
}

std::vector<std::uint8_t>
vignetteImage(std::uint32_t size, const VignetteShape& shape, float quadHalfDegrees) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(size) * size * 4, 0);
    if (size == 0) {
        return px;
    }
    const float half = std::clamp(quadHalfDegrees, 1.0f, 89.0f);
    const float edgeTangent = std::tan(half / kDegrees);
    const float opacity = std::isfinite(shape.opacity) ? std::clamp(shape.opacity, 0.0f, 1.0f) : 0.0f;
    const float centre = static_cast<float>(size) / 2.0f;
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            // The pixel's direction on the quad's plane, as a tangent from the view axis.
            const float u = (static_cast<float>(x) + 0.5f - centre) / centre * edgeTangent;
            const float v = (static_cast<float>(y) + 0.5f - centre) / centre * edgeTangent;
            const float degrees = std::atan(std::sqrt(u * u + v * v)) * kDegrees;
            const float alpha = opacity * smoothstep(shape.clearDegrees, shape.darkDegrees, degrees);
            // Premultiplied black: the colour stays 0, only the coverage changes.
            px[(static_cast<std::size_t>(y) * size + x) * 4 + 3] =
                static_cast<std::uint8_t>(std::lround(alpha * 255.0f));
        }
    }
    return px;
}

} // namespace evr::comfort
