#pragma once

// Reach and press to use (docs/VR_INTERACTIONS.md section 2.3). Off by default.
//
// Reaching a hand out ahead of the face and pulling it back, as when pressing a switch, tapping a door panel
// or taking a pickup, presses the game's `_use` button alone (GameAction::Use). The game uses what is in its
// focus, and with nothing in focus the button does nothing; the melee press (`_attack2 _use`) swings at the
// air instead.
//
// A reach is an out-and-back of one hand, measured from the eyes along the head's heading:
//
// - out: from inside `rearmDistance`, the hand goes past `reachDistance` ahead in one movement (no pause
//   on the way), having moved forward at `minSpeed` or more and never faster than `maxSpeed` (faster is a
//   punch, which presses use as part of melee itself);
// - back: within `maxOutSeconds` of passing `reachDistance`, the hand comes back at least `withdrawDistance`
//   from the furthest it went, never having dropped below the height it had there by more than it came back
//   plus `dropAllowance`.
//
// The press goes out on the way back. Raising the weapon to aim, or bringing the other hand up to steady it,
// is an "out" that stays out, and an arm lowered from the shoulder falls before it comes back: neither
// presses. What is left is a weapon raised and pulled straight back within `maxOutSeconds` without a shot,
// which reads as a reach (docs/VR_INTERACTIONS.md section 2.3). The trigger pulled at any
// point (firing) ends the reach, as does a hand primed for the overhead swing
// (arm_gestures.hpp). One press per reach: the hand has to come back inside
// `rearmDistance` before the next, nothing fires within `cooldownSeconds` of a press, and a hand whose
// tracking starts or resumes out there has to come back first. Either hand reaches.

#include "features/input/controller_state.hpp"

#include <array>

namespace evr::input {

struct ReachSettings {
    bool enabled = false;
    float reachDistance = 0.45f; // metres ahead of the eyes, along the heading
    float rearmDistance = 0.30f; // the hand is back when it is nearer than this
    float minSpeed = 0.6f;       // forward, metres per second, at some point on the way out
    float maxSpeed = 2.4f; // forward, never on the way out: faster is a punch (the punch's 2.8 by default)
    float withdrawDistance = 0.12f; // metres back from the furthest point: the press
    float dropAllowance =
        0.03f;                  // metres the hand may drop beyond how far it came back: more is a lowered arm
    float maxOutSeconds = 0.8f; // from passing reachDistance to the press: longer is a hold (aiming)
    float maxTrigger = 0.2f;    // the hand's trigger (0..1) must stay below this
    float cooldownSeconds = 0.5f; // between presses
    float holdSeconds = 0.3f;     // how long use stays down after a reach (see useDown)
};

struct ReachOutput {
    bool reached = false;       // a reach pressed use this frame
    std::array<bool, 2> hand{}; // per hand (indexed by Hand): the one that reached
    // Use is down: from the press for `holdSeconds`, as a button pressed by hand. The game takes a use only
    // on some of its frames: a press of a frame or two is often not seen (rig, docs/VR_INTERACTIONS.md 2.3).
    bool useDown = false;
};

class ReachUse {
public:
    // Values that are not finite or out of range fall back to the defaults.
    explicit ReachUse(ReachSettings settings = {});

    // `dtSeconds` must be finite and non-negative. `primed`: per hand, an overhead swing is primed
    // (ArmGestureOutput::heldBack). `blocked`: the game holds the gestures back (a cutscene, a forced view,
    // the weapon wheel up, ...): nothing fires and every hand has to come back first.
    ReachOutput
    update(const InputFrame& frame, float dtSeconds, std::array<bool, 2> primed = {}, bool blocked = false);

    [[nodiscard]] const ReachSettings& settings() const { return settings_; }

private:
    struct HandReach {
        bool armed = false;     // back inside rearmDistance since the last reach (or the tracking's start)
        bool out = false;       // past reachDistance on a reach that may still come back
        float peakSpeed = 0.0f; // the fastest forward speed since the hand was last inside rearmDistance
        float outSeconds = 0.0f;
        float furthest = 0.0f;    // metres ahead, the furthest the reach went
        float heightThere = 0.0f; // metres above the eyes, at the furthest point
    };

    ReachSettings settings_;
    std::array<HandReach, 2> hands_{};
    float cooldown_ = 0.0f;
    float holding_ = 0.0f; // seconds left of the use held down
};

} // namespace evr::input
