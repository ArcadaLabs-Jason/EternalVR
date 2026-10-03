#pragma once

// The in-headset capture's chord (docs/VR_CONTROLLERS.md): while the left Menu button is held, pulling
// either trigger asks for a capture of both eyes for a bug report, one per pull.
//
// The chord takes both buttons away from what they normally do:
// - while Menu is held, both triggers are held back (no fire, no equipment, no menu click), and a trigger
//   pulled meanwhile stays held back until it is let go, even when Menu goes up first;
// - a Menu press during which a capture fired neither pauses on its release nor recenters (cancelMenu).
// The left Menu button is the one in every handedness: on Touch controllers it is the only Menu button an
// application can read (the right one belongs to the system). No built-in map puts a gameplay action on it.
//
// SteamVR keeps the left Menu button of Touch controllers for its dashboard (dashboard_pause.hpp), so with
// that runtime and family both sticks held as the recenter chord (stick_chord.hpp) work as the chord's
// button too, from the hold time on: no Touch button is free in every map, and the stick chord already
// takes both sticks away from their bindings. It holds back only the trigger pulled while it is held, not
// one already down, and a stick chord during which a capture fired does not recenter (cancelSticks).
//
// Pure: the mapper and the menu pointer each run one on the same controller frames.

#include "features/input/analog_button.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/stick_chord.hpp"
#include "features/input/tap_hold.hpp"

#include <array>

namespace evr::input {

// The buttons a trigger pull can be chorded with.
enum class CaptureButtons {
    Menu,         // the left Menu button
    MenuOrSticks, // ... or both sticks held, where the runtime keeps the Menu button
};

struct CaptureChordOutput {
    bool capture = false;      // one frame: a trigger went down while the chord's button is held
    bool cancelMenu = false;   // the Menu press going on had a capture: it must not tap or hold
    bool cancelSticks = false; // the stick chord going on had a capture: it must not recenter
    // Per hand (indexed by Hand): the trigger's bindings are held back this frame.
    std::array<bool, 2> triggerHeldBack{};
};

class CaptureChord {
public:
    // `holdSeconds`: how long both sticks are held before a pull captures (the mapper's hold time).
    explicit CaptureChord(AnalogThresholds trigger = kTriggerThresholds,
                          CaptureButtons buttons = CaptureButtons::Menu,
                          float holdSeconds = kDefaultHoldSeconds);

    // `dtSeconds`: the time since the last frame (the sticks' hold).
    CaptureChordOutput update(const InputFrame& frame, float dtSeconds);

    [[nodiscard]] CaptureButtons buttons() const { return buttons_; }

private:
    std::array<AnalogButton, 2> triggers_;
    CaptureButtons buttons_;
    StickChord sticks_;
    std::array<bool, 2> heldBack_{};
    bool capturedMenu_ = false;   // a capture fired during the Menu press going on
    bool capturedSticks_ = false; // ... during the stick chord going on
};

} // namespace evr::input
