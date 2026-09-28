#pragma once

// The in-headset capture's chord (docs/VR_CONTROLLERS.md): while the left Menu button is held, pulling
// either trigger asks for a capture of both eyes for a bug report, one per pull.
//
// The chord takes both buttons away from what they normally do:
// - while Menu is held, both triggers are held back (no fire, no equipment, no menu click), and a trigger
//   pulled meanwhile stays held back until it is let go, even when Menu goes up first;
// - a Menu press during which a capture fired neither pauses on its release nor recenters (cancelMenu).
// The left Menu button is the one in every handedness: on Touch controllers it is the only Menu button an
// application can read (the right one belongs to the system).
//
// Pure: the mapper and the menu pointer each run one on the same controller frames.

#include "features/input/analog_button.hpp"
#include "features/input/controller_state.hpp"

#include <array>

namespace evr::input {

struct CaptureChordOutput {
    bool capture = false;    // one frame: a trigger went down while Menu is held
    bool cancelMenu = false; // the Menu press going on had a capture: it must not tap or hold
    // Per hand (indexed by Hand): the trigger's bindings are held back this frame.
    std::array<bool, 2> triggerHeldBack{};
};

class CaptureChord {
public:
    explicit CaptureChord(AnalogThresholds trigger = kTriggerThresholds);

    CaptureChordOutput update(const InputFrame& frame);

private:
    std::array<AnalogButton, 2> triggers_;
    std::array<bool, 2> heldBack_{};
    bool captured_ = false; // a capture fired during the Menu press going on
};

} // namespace evr::input
