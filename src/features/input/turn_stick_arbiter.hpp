#pragma once

// Decides what each movement of the turn stick means: turning, or a selection gesture.
//
// Weapon swaps triggered by turning are a deal-breaker for players (R14 section 2.7, failure mode 4:
// "lots of unwanted weapon swapping while mid fights"). The rule that prevents it:
//
//   A sweep runs from the stick leaving the centre until it comes back within `centreRadius`. Each
//   sweep has exactly one intent, claimed by whichever qualifies first, and keeps it until it ends:
//     - Up / Down: deflection of at least `gestureEngage` within `claimConeDegrees` of straight
//       up or down;
//     - Turn: horizontal deflection of at least `turnClaim` outside those cones.
//   A sweep claimed by turning can never select a weapon, and a selection sweep never turns.
//
//   A down sweep must also stay deliberate: if the stick leaves the wider `stayConeDegrees` before
//   the hold time, the sweep is cancelled and does nothing. Returning to the centre before the hold
//   time is a quick switch; holding past it opens the wheel, after which the stick points at the
//   wheel freely until it is released.
//
// So a turn that drifts downward, a roll from turning into pulling back, or a stick that wanders
// while held can never produce a weapon change.
//
// The wheel can also be held by a button (a player's map may bind weapon_wheel to one). While it is,
// the stick points at the wheel as it does after a down hold: the sweep in progress is cancelled, and
// one still out of the centre when the button is let go stays cancelled until it comes back, so
// pointing at the wheel never turns, fires a gesture or ends in a quick switch.

#include "features/input/axis2.hpp"

#include <cstdint>

namespace evr::input {

struct TurnStickSettings {
    float centreRadius = 0.25f;
    float turnClaim = 0.35f;
    float gestureEngage = 0.75f;
    float claimConeDegrees = 30.0f;
    float stayConeDegrees = 45.0f;
    // Hold to open the wheel. This delay is ours, on the stick, before any action is sent; the game
    // may wait a delay of its own on top.
    float holdSeconds = 0.30f;
};

enum class SweepIntent : std::uint8_t {
    None, // Centred, or deflected without qualifying yet.
    Turn,
    Up,
    Down,
    Cancelled,
};

struct TurnStickOutput {
    bool turnAllowed = false;
    bool up = false;       // For the whole up sweep.
    bool downTap = false;  // One frame, when a short down sweep ends.
    bool downHold = false; // From the hold time until the sweep ends.
    Axis2 wheelPointer;    // The stick, while downHold is active or a button holds the wheel.
};

class TurnStickArbiter {
public:
    // Settings that are not finite, out of range or inconsistent with each other (the centre radius
    // must be the smallest deflection, the stay cone at least the claim cone) fall back to the
    // defaults as a whole.
    explicit TurnStickArbiter(TurnStickSettings settings = {});

    // `dtSeconds` must be finite and non-negative. A non-finite stick (a lost action state) is
    // treated as the last finite position with no time passing, so a glitch neither ends a sweep,
    // which would fire a quick switch, nor moves the hold timer. `wheelHeld`: a button holds the
    // weapon wheel this frame, and the stick only points at it.
    TurnStickOutput update(Axis2 stick, float dtSeconds, bool wheelHeld = false);

    [[nodiscard]] SweepIntent intent() const { return intent_; }
    [[nodiscard]] const TurnStickSettings& settings() const { return settings_; }

private:
    SweepIntent claim(Axis2 stick) const;
    TurnStickOutput continueDown(Axis2 stick);
    TurnStickOutput pointAtWheel(Axis2 stick);

    TurnStickSettings settings_;
    SweepIntent intent_ = SweepIntent::None;
    float downSeconds_ = 0.0f;
    bool holdReached_ = false;
    Axis2 lastStick_;
};

} // namespace evr::input
