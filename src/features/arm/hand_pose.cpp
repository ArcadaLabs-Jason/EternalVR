#include "features/arm/hand_pose.hpp"

#include "features/arm/arm_frames.hpp"
#include "features/arm/left_hand.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace evr::arm {

namespace {

float clamp01(float v) {
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
}

float curlOf(const FingerCurls& c, int finger) {
    switch (finger) {
    case 0:
        return c.thumb;
    case 1:
        return c.index;
    case 2:
        return c.middle;
    case 3:
        return c.ring;
    default:
        return c.pinky;
    }
}

} // namespace

FingerCurls fingerCurls(const FingerSensors& s) {
    const float grip = clamp01(s.grip);
    const float index = std::max(clamp01(s.trigger), s.triggerTouch ? kRestingIndexCurl : 0.0f);
    return {s.thumbTouch ? 1.0f : 0.0f, index, grip, grip, grip};
}

FingerCurls FingerEase::update(const FingerCurls& wanted, float dtSeconds, float seconds) {
    if (!started_) {
        started_ = true;
        now_ = wanted;
        return now_;
    }
    const float dt = std::isfinite(dtSeconds) ? std::max(0.0f, dtSeconds) : 0.0f;
    const float k = seconds > 0.0f ? 1.0f - std::exp(-dt / seconds) : 1.0f;
    const auto ease = [k](float now, float want) {
        return now + (want - now) * k;
    };
    now_ = {ease(now_.thumb, wanted.thumb), ease(now_.index, wanted.index), ease(now_.middle, wanted.middle),
            ease(now_.ring, wanted.ring), ease(now_.pinky, wanted.pinky)};
    return now_;
}

float FingerHold::update(bool tracked, float dtSeconds, float seconds) {
    const float wanted = tracked ? 1.0f : 0.0f;
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

int fingerOf(std::size_t joint) {
    return joint < 3 ? 0 : static_cast<int>(1 + (joint - 3) / 4);
}

HandLocal openHand(const HandLocal& grip) {
    HandLocal out = grip;
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        if (kHandJointParents[i] >= 0) {
            out[i].axis = xr_math::IdViewAxis{};
        }
    }
    return out;
}

const HandLocal& leftGripHand() {
    static const HandLocal hand = mirroredGrip(heavyCannonGrip()).hand;
    return hand;
}

HandLocal posedHand(const HandLocal& open,
                    const HandLocal& closed,
                    const FingerCurls& curls,
                    const HandLocal& offsets) {
    HandLocal out;
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        const float c = clamp01(curlOf(curls, fingerOf(i)));
        out[i] = {offsets[i].position, blendPose(open[i], closed[i], c).axis};
    }
    return out;
}

HandLocal blendHand(const HandLocal& game, const HandLocal& posed, float weight) {
    const float w = clamp01(weight);
    HandLocal out;
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        out[i] = blendPose(game[i], posed[i], w);
    }
    return out;
}

std::array<xr_math::ModelPose, kFingerModCount> fingerWrites(const HandLocal& hand) {
    std::array<xr_math::ModelPose, kFingerModCount> out;
    for (std::size_t k = 0; k < kFingerModCount; ++k) {
        out[k] = hand[kFingerMods[k]];
    }
    return out;
}

} // namespace evr::arm
