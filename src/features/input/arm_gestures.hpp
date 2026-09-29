#pragma once

// Two deliberate arm gestures that press a game action (docs/VR_INTERACTIONS.md). Both are off by default.
//
// - Throw: the off hand winds up beside the head, as for an overhand throw, then swings forward. It presses
//   the equipment launcher (a grenade). Wound up means at most `windupMinHeight` below the eyes and no
//   further ahead of them than `windupMaxForward`: beside or behind the ear, where no punch, support grip
//   or rest pose puts the hand.
// - Overhead swing: the weapon hand is raised above the head, then brought down hard. It presses the
//   Crucible (the Sentinel Hammer in The Ancient Gods Part Two). Raised means at least `raiseMinHeight`
//   above the eyes, with the other hand below that: both hands up is a stretch or the hands-jump gesture,
//   and that pose never primes the swing until the weapon hand has come down once.
//
// A pose primes its gesture for `primeSeconds` after the hand leaves it; the gesture fires on the first
// frame within that window where the hand moves faster than its speed in the gesture's direction (head
// forward for the throw, straight down for the swing). After a gesture the pose has to be taken again, and
// nothing fires within `cooldownSeconds`. A hand whose tracking starts or resumes mid-motion must take the
// pose first. Distances are measured from the head in its heading (the head's forward projected on the
// floor), so they work in any facing, seated and standing.
//
// While a hand is primed its punch is held back (`heldBack`): the swing of a throw or a chop would
// otherwise punch as well.

#include "features/input/controller_state.hpp"

#include <array>

namespace evr::input {

// Range offered for the gesture speeds (metres per second).
inline constexpr float kMinGestureSpeed = 1.0f;
inline constexpr float kMaxGestureSpeed = 5.0f;

struct ThrowSettings {
    bool enabled = false;
    float speed = 2.0f;             // forward, along the head's heading
    float windupMinHeight = -0.15f; // metres from the eyes (negative: below them)
    float windupMaxForward = 0.10f; // metres ahead of the eyes, along the heading
    float primeSeconds = 0.8f;
    float cooldownSeconds = 0.5f;
};

struct SwingSettings {
    bool enabled = false;
    float speed = 2.5f;           // downward
    float raiseMinHeight = 0.10f; // metres above the eyes
    float primeSeconds = 0.8f;
    float cooldownSeconds = 0.5f;
};

struct ArmGestureOutput {
    bool thrown = false; // the off hand threw this frame: press the equipment launcher
    bool swung = false;  // the weapon hand swung down this frame: press the Crucible
    // Per hand (indexed by Hand): primed for a gesture or firing one, so its punch is held back.
    std::array<bool, 2> heldBack{};
};

class ArmGestures {
public:
    // Values that are not finite or out of range fall back to the defaults; the speeds are clamped to the
    // range above.
    explicit ArmGestures(ThrowSettings throwSettings = {}, SwingSettings swingSettings = {});

    // `dtSeconds` must be finite and non-negative.
    ArmGestureOutput update(const InputFrame& frame, Hand weaponHand, float dtSeconds);

    [[nodiscard]] const ThrowSettings& throwSettings() const { return throw_; }
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

    ThrowSettings throw_;
    SwingSettings swing_;
    Primer throwPrimer_;
    Primer swingPrimer_;
    // Both hands were raised together: the swing waits until the weapon hand has come down.
    bool bothRaised_ = false;
    Hand weaponHand_ = Hand::Right;
};

} // namespace evr::input
