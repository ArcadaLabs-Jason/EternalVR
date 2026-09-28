#pragma once

// Jump by throwing both hands up above the head (R06 section 5).
//
// Off by default in every preset: players jump constantly and two-handed aiming raises both
// hands often, so false jumps are likely (R06). When enabled it still stays off for seated players
// unless they opt in, because a seated player's hands are much closer to head height. An unknown
// posture does not block it; that happens only without a floor-relative space, and the player chose
// to enable the gesture.
//
// To fire, both hands must be at or above head height and each must have been moving upward fast
// within a short window, since the arms slow down as they reach full extension. After a jump both
// hands must settle before the next one, and tracking that starts or resumes mid-motion never fires.

#include "features/input/controller_state.hpp"
#include "features/posture/posture_detector.hpp"

namespace evr::input {

struct HandsJumpSettings {
    bool enabled = false;
    bool allowWhenSeated = false;
    float minUpwardSpeed = 1.9f; // metres per second, each hand
    float settleSpeed = 0.5f;    // both hands below this re-arm the gesture
    float minHeightAboveHead = 0.0f;
    float fastWindowSeconds = 0.15f;
    float cooldownSeconds = 0.35f;
};

class HandsJumpDetector {
public:
    explicit HandsJumpDetector(HandsJumpSettings settings = {}) : settings_(settings) {}

    // Returns true on the frame the gesture fires. `dtSeconds` must be finite and non-negative.
    bool update(const InputFrame& frame, posture::Posture posture, float dtSeconds);

private:
    bool active(const InputFrame& frame, posture::Posture posture) const;
    void reset();

    HandsJumpSettings settings_;
    bool armed_ = false;
    float cooldown_ = 0.0f;
    float sinceLeftFast_ = 0.0f;
    float sinceRightFast_ = 0.0f;
    bool leftSeenFast_ = false;
    bool rightSeenFast_ = false;
};

} // namespace evr::input
