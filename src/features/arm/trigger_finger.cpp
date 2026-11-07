#include "features/arm/trigger_finger.hpp"

#include "features/arm/arm_frames.hpp"

#include <algorithm>
#include <cmath>

namespace evr::arm {

namespace {

float clampTo(float v, float hi) {
    return std::isfinite(v) ? std::clamp(v, 0.0f, hi) : 0.0f;
}

// `q` with its angle scaled by `scale` about the same axis (the shorter way round first).
Quat scaledTurn(Quat q, float scale) {
    if (q.w < 0.0f) {
        q = {-q.x, -q.y, -q.z, -q.w};
    }
    const float s = length(Vec3{q.x, q.y, q.z});
    if (!(s > 1e-7f)) {
        return Quat::identity();
    }
    const float half = std::atan2(s, q.w) * scale;
    const float k = std::sin(half) / s;
    return {q.x * k, q.y * k, q.z * k, std::cos(half)};
}

} // namespace

float triggerOpen(const TriggerSensors& sensors, bool touchSensed, float lift) {
    const float pulled = clampTo(sensors.trigger, 1.0f);
    const bool off = touchSensed && !sensors.touch && pulled <= kTriggerFree;
    return off ? clampTo(lift, kMaxLift) : kRestOpen * (1.0f - pulled);
}

float TriggerFinger::update(
    const TriggerSensors& sensors, bool tracked, float lift, float dtSeconds, float seconds) {
    touchSeen_ = tracked && (touchSeen_ || sensors.touch);
    const float wanted = triggerOpen(sensors, touchSeen_, lift);
    if (!started_) {
        started_ = true;
        now_ = wanted;
        return now_;
    }
    const float dt = std::isfinite(dtSeconds) ? std::max(0.0f, dtSeconds) : 0.0f;
    const float k = seconds > 0.0f ? 1.0f - std::exp(-dt / seconds) : 1.0f;
    now_ += (wanted - now_) * k;
    return now_;
}

HandLocal openIndex(const HandLocal& hand, float open) {
    HandLocal out = hand;
    const float scale = std::isfinite(open) ? 1.0f - open : 1.0f;
    for (const std::size_t i : kIndexJoints) {
        out[i].axis = fromQuat(scaledTurn(toQuat(hand[i].axis), scale));
    }
    return out;
}

IndexChoice indexChoice(std::optional<float> open, bool glove, bool gripRead, float armWeight) {
    if (!open) {
        return {};
    }
    if (glove) {
        return {IndexChoice::From::Glove, *open, 0};
    }
    if (!gripRead) {
        return {};
    }
    const float weight = std::isfinite(armWeight) ? std::clamp(armWeight, 0.0f, 1.0f) : 0.0f;
    return {IndexChoice::From::Grip, *open * weight, kIndexModMask};
}

} // namespace evr::arm
