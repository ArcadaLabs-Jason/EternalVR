#pragma once

// Jump by throwing both hands up above the head (R06 section 5).
//
// Off by default in every preset: players jump constantly and two-handed aiming raises both
// hands often, so false jumps are likely (R06).
//
// To fire, both hands must be at or above head height and each must have been moving upward fast
// within a short window, since the arms slow down as they reach full extension. After a jump both
// hands must settle before the next one, and tracking that starts or resumes mid-motion never fires.
//
// Seated, the hands must go higher (seatedExtraHeight): a seated player rests the hands on the head,
// reaches to the headset or leans back with them behind the head far more often than a standing one.
// An unknown posture (no floor-relative space) uses the standing height.

#include "features/input/controller_state.hpp"
#include "features/posture/posture_detector.hpp"

namespace evr::input {

struct HandsJumpSettings {
    bool enabled = false;
    float minUpwardSpeed = 1.9f; // metres per second, each hand
    float settleSpeed = 0.5f;    // both hands below this re-arm the gesture
    float minHeightAboveHead = 0.0f;
    // Added to minHeightAboveHead when seated. The head pose is at the eyes, and the crown of the head
    // (where the hands go to adjust the headset's strap) is about 0.12 m above them; arms thrown straight
    // up put the controllers about 0.4 m above the eyes, seated or standing. 0.15 m clears the crown and
    // is still well below a full throw, which slows only near full extension. The speed stays the same:
    // a seated throw is no slower.
    float seatedExtraHeight = 0.15f;
    float fastWindowSeconds = 0.15f;
    float cooldownSeconds = 0.35f;
};

class HandsJumpDetector {
public:
    explicit HandsJumpDetector(HandsJumpSettings settings = {}) : settings_(settings) {}

    // Returns true on the frame the gesture fires. `dtSeconds` must be finite and non-negative.
    bool update(const InputFrame& frame, posture::Posture posture, float dtSeconds);

    // How far above the head (metres) both hands must reach in `posture`.
    [[nodiscard]] float heightAboveHead(posture::Posture posture) const;

private:
    bool active(const InputFrame& frame) const;
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
