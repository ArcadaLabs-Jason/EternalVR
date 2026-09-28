#pragma once

// The recenter chord (docs/VR_CONTROLLERS.md): both thumbsticks pressed and held. Held for the recenter time
// (ETERNALVR_RECENTER_HOLD, 2 s by default) it re-anchors the room; the Menu button is left to the pause
// alone, because Virtual Desktop and the Quest's own menu watch a held Menu button.
//
// A single stick click stays instant: its bindings (melee, the crucible) see it the frame it goes down. Only
// a stick pressed while the other is already down waits, at most `windowSeconds`:
// - pressed within `windowSeconds` of the other stick, or still held after `windowSeconds`: the chord. Both
//   sticks' bindings are released and stay released until each stick is let go (the first stick's action
//   has already fired: a two-stick press cannot be told from a single click at the first press);
// - let go again before `windowSeconds` (a quick second click while the first stick is held): a click, sent
//   late, for one frame.
//
// `recenter` is the chord's level, on from the hold time (the mapper's hold, kDefaultHoldSeconds) after the
// second stick went down, like a binding's hold, so the room-scale long press counts the rest of the
// recenter time from there. Pure: time comes from the frame delta.

#include "features/input/tap_hold.hpp"

#include <array>

namespace evr::input {

inline constexpr float kStickChordWindowSeconds = 0.15f;

struct StickChordOutput {
    std::array<bool, 2> click{}; // indexed by Hand: the stick click the bindings see
    bool recenter = false;       // both sticks held as a chord for at least the hold time
    bool chord = false;          // both sticks are held as a chord (the recenter may not have begun yet)
};

class StickChord {
public:
    explicit StickChord(float holdSeconds = kDefaultHoldSeconds,
                        float windowSeconds = kStickChordWindowSeconds);

    // `left` / `right`: the sticks' clicks this frame; `dtSeconds` the time since the last frame.
    StickChordOutput update(bool left, bool right, float dtSeconds);

private:
    float holdSeconds_;
    float windowSeconds_;
    std::array<bool, 2> down_{};
    std::array<float, 2> held_{};      // seconds since each stick went down
    std::array<bool, 2> suppressed_{}; // released by a chord until the stick is let go
    std::array<bool, 2> withheld_{};   // pressed while the other was down: waiting for the window
    bool chord_ = false;
    float chordSeconds_ = 0.0f; // since the second stick went down
};

} // namespace evr::input
