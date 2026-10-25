#pragma once

// A deliberate arm gesture that presses a game action (docs/VR_INTERACTIONS.md), off by default.
//
// Overhead swing: the weapon hand is raised above the head, then brought down hard. It presses the
// Crucible (the Sentinel Hammer in The Ancient Gods Part Two). Raised means at least `raiseMinHeight` above
// the eyes, with the other hand below that: both hands up is a stretch or the hands-jump gesture, and that
// pose never primes the swing until the weapon hand has come down once.
//
// The pose primes the swing for `primeSeconds` after the hand leaves it; the swing fires on the first frame
// within that window where the hand moves down faster than its speed. After a swing the pose has to be taken
// again, and nothing fires within `cooldownSeconds`. A hand whose tracking starts or resumes mid-motion must
// take the pose first. Heights are measured from the eyes, so they work in any facing, seated and standing.
//
// While the weapon hand is primed its punch is held back (`heldBack`): the chop would otherwise punch as
// well.

#include "features/input/controller_state.hpp"

#include <array>

namespace evr::input {

// Range offered for the gesture speeds (metres per second).
inline constexpr float kMinGestureSpeed = 1.0f;
inline constexpr float kMaxGestureSpeed = 5.0f;

struct SwingSettings {
    bool enabled = false;
    float speed = 2.5f;           // downward
    float raiseMinHeight = 0.10f; // metres above the eyes
    float primeSeconds = 0.8f;
    float cooldownSeconds = 0.5f;
};

struct ArmGestureOutput {
    bool swung = false; // the weapon hand swung down this frame: press the Crucible
    // Per hand (indexed by Hand): primed for a gesture or firing one, so its punch is held back.
    std::array<bool, 2> heldBack{};
};

class ArmGestures {
public:
    // Values that are not finite or out of range fall back to the defaults; the speeds are clamped to the
    // range above.
    explicit ArmGestures(SwingSettings swingSettings = {});

    // `dtSeconds` must be finite and non-negative.
    ArmGestureOutput update(const InputFrame& frame, Hand weaponHand, float dtSeconds);

    [[nodiscard]] const SwingSettings& swingSettings() const { return swing_; }

private:
    // One gesture's timing: primed while the pose is held and for a while after, and a cooldown.
    struct Primer {
        float sincePose = -1.0f; // seconds since the hand was last in the pose; negative: not primed
        float cooldown = 0.0f;

        [[nodiscard]] bool primed(float primeSeconds) const {
            return sincePose >= 0.0f && sincePose <= primeSeconds;
        }
        // Advances by `dt` with the hand in the pose or not; true when `fast` fires the gesture.
        bool step(bool inPose, bool fast, float dt, float primeSeconds, float cooldownSeconds);
        void reset() { *this = {}; }
    };

    SwingSettings swing_;
    Primer swingPrimer_;
    // Both hands were raised together: the swing waits until the weapon hand has come down.
    bool bothRaised_ = false;
    Hand weaponHand_ = Hand::Right;
};

} // namespace evr::input
