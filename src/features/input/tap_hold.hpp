#pragma once

// Splits one button into a tap action and a hold action (R06: one face button can carry two actions).
//
// A tap fires on release, because until then it cannot be told apart from the start of a hold. Time
// comes from the frame delta, so results are deterministic for a given sequence of samples.

#include "common/finite.hpp"

namespace evr::input {

// Short enough to feel immediate for the hold action, long enough that a quick tap never opens it.
inline constexpr float kDefaultHoldSeconds = 0.25f;
inline constexpr float kMaxHoldSeconds = 5.0f;

struct TapHoldOutput {
    bool tap = false;  // One frame, on a release before the tap time (by default the hold time).
    bool hold = false; // From the hold time until release.
};

class TapHoldDetector {
public:
    // A hold time that is not finite or outside 0 to kMaxHoldSeconds falls back to the default.
    explicit TapHoldDetector(float holdSeconds = kDefaultHoldSeconds)
        : holdSeconds_(finiteInRangeOr(holdSeconds, 0.0f, kMaxHoldSeconds, kDefaultHoldSeconds)),
          tapSeconds_(holdSeconds_) {}

    // A release before `tapSeconds` (at least the hold time) still counts as a tap, even after the hold
    // began: for a button whose hold action completes only later (the Menu button's recenter, 1 s), so a
    // press between the hold time and that point is not lost. Not finite or below the hold time: the hold
    // time.
    TapHoldDetector(float holdSeconds, float tapSeconds) : TapHoldDetector(holdSeconds) {
        tapSeconds_ = finiteInRangeOr(tapSeconds, holdSeconds_, kMaxHoldSeconds, holdSeconds_);
    }

    // `dtSeconds` is the time since the previous sample; it must be finite and non-negative. The
    // frame on which the button goes down counts as time zero.
    TapHoldOutput update(bool down, float dtSeconds);

    // The press going on (or starting with the next update) is used up by something else (the Menu
    // button's capture chord): from the next update until its release it is neither a hold nor a tap.
    void cancel() { cancelled_ = true; }

    // The button was down at the last update.
    [[nodiscard]] bool isDown() const { return wasDown_; }

    [[nodiscard]] float holdSeconds() const { return holdSeconds_; }
    [[nodiscard]] float tapSeconds() const { return tapSeconds_; }

private:
    float holdSeconds_;
    float tapSeconds_;
    float heldSeconds_ = 0.0f;
    bool wasDown_ = false;
    bool cancelled_ = false;
};

} // namespace evr::input
