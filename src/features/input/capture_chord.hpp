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
// SteamVR keeps the left Menu button for its dashboard (dashboard_pause.hpp), so with that runtime the left
// secondary button (Y on Touch) works as the chord's button too. It holds back only the trigger pulled
// while it is held, not one already down: a Y tap (switch weapon mod) while firing keeps firing. A Y press
// during which a capture fired neither taps nor holds (no mod switch, no pause).
//
// Pure: the mapper and the menu pointer each run one on the same controller frames.

#include "features/input/analog_button.hpp"
#include "features/input/controller_state.hpp"

#include <array>

namespace evr::input {

// The left-hand buttons a trigger pull can be chorded with.
enum class CaptureButtons {
    Menu,
    MenuOrSecondary, // SteamVR: the Menu button may never arrive
};

struct CaptureChordOutput {
    bool capture = false;         // one frame: a trigger went down while the chord's button is held
    bool cancelMenu = false;      // the Menu press going on had a capture: it must not tap or hold
    bool cancelSecondary = false; // the same for the secondary button's press
    // Per hand (indexed by Hand): the trigger's bindings are held back this frame.
    std::array<bool, 2> triggerHeldBack{};
};

class CaptureChord {
public:
    explicit CaptureChord(AnalogThresholds trigger = kTriggerThresholds,
                          CaptureButtons buttons = CaptureButtons::Menu);

    CaptureChordOutput update(const InputFrame& frame);

    [[nodiscard]] CaptureButtons buttons() const { return buttons_; }

private:
    std::array<AnalogButton, 2> triggers_;
    CaptureButtons buttons_;
    std::array<bool, 2> heldBack_{};
    bool capturedMenu_ = false;      // a capture fired during the Menu press going on
    bool capturedSecondary_ = false; // ... during the secondary button's press going on
};

} // namespace evr::input
