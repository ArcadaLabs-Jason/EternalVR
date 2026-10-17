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
    // Hold before a down sweep is a hold (downHold). This delay is ours, on the stick, before any action is
    // sent; the game may wait a delay of its own on top.
    float holdSeconds = 0.30f;
    // A down sweep becomes a hold sooner, at earlyHoldSeconds, when the stick is then within earlyConeDegrees
    // of straight down: a deliberate push, not one already rolling toward a turn. At or above holdSeconds
    // (the default) there is no early hold. The mapper sets it when the down hold opens the weapon wheel and
    // nothing else (kWheelStickHoldSeconds).
    float earlyHoldSeconds = 0.30f;
    float earlyConeDegrees = 20.0f;
};

// The down hold when it opens the weapon wheel and nothing else (the built-in maps). The wheel's key is also
// the quick switch's (Q, game/eternal/usercmd_buttons.cpp), and the game tells a tap of it from a hold after
// its own open delay (weaponWheel_HoldTimeForOpeningWheel, the game's Weapon Wheel Open Delay, 180 ms by
// default): a down sweep let go within that delay of the down hold is still a quick switch, never the wheel.
// So the stick's hold only has to tell a deliberate push down from a sweep that drifts down and leaves the
// stay cone, not a flick from a hold: 0.15 s for a push within 20 degrees of straight down (the early hold;
// it was 0.30 s, 0.48 s in all from the push to the wheel), the full hold (0.30 s) otherwise.
//
// The trade-off (R14 failure mode 4, unwanted weapon swaps): a down sweep that rolls out of the stay cone
// after the early hold is a hold, no longer cancelled, and its release swaps the weapon (a quick switch while
// the game's delay runs, the highlighted weapon after). Up to 0.1.50 that held for any roll within 0.30 s.
// A roll already under way at 0.15 s (past 20 degrees) is still cancelled when it leaves the cone.
inline constexpr float kWheelStickHoldSeconds = 0.15f;
// The least of it, at the shortest Hold time.
inline constexpr float kMinWheelStickHoldSeconds = 0.10f;

// The wheel's down hold for the buttons' hold time (the launcher's Hold time): 0.10 s shorter, at least
// kMinWheelStickHoldSeconds (Short 0.15 s -> 0.10, Medium 0.25 -> 0.15, Long 0.4 -> 0.30, Very long 0.6 ->
// 0.50). A hold time that is not finite gives kWheelStickHoldSeconds.
float wheelStickHoldSeconds(float buttonHoldSeconds);

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

    // The sweep going on, if any, is used up by something else (a menu it began under): it does nothing
    // more, a quick switch included, until the stick is back in the centre.
    void cancelSweep();

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
