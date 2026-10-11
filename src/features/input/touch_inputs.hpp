#pragma once

// The touch sensors a control map can bind like buttons (docs/VR_CONTROLLERS.md, "Touch sensors as
// buttons"; DECISIONS T-126): the thumb rest (touch_button.hpp, T-125), and the touch of the primary and
// secondary buttons (A/X, B/Y), the stick, the trackpad and the trigger.
//
// - Where: the sensor's leaf on the family's interaction profile, only where the profile lists it
//   (interaction_profiles.cpp): the primary and secondary buttons' touch is the touch of the A, B, X or Y
//   button the family's data binds to them (none where that is a trackpad or a D-pad), the others have one
//   path each. Built-in data suggests no binding for them unless the thumb-rest wheel adds one; a control map
//   binding one gets its binding added for the session (rest_touch_bindings.hpp).
// - Its own controls: the controls the thumb or finger on the sensor is about to use. For the thumb's touches
//   (A/X, B/Y, the stick, the trackpad) every control under that thumb: one thumb works them all, so using
//   any of them means it left the touch, and a touch that slides from A onto B, or rests on B while A is
//   pressed, is no tap of either. Read from the family's data: the gameplay bindings on the controller's face
//   (A, B, X, Y, the stick, the trackpad, the Steam Frame's D-pad, Menu and View; on the Vive wands the stick
//   is the trackpad, on Index the left Menu button is the trackpad pressed). For the trigger's touch only the
//   trigger past its press threshold (the bindings that share its identifier). A default for the plain
//   layout where there is no data.
//
// Pure data and lookups, no OpenXR.

#include "features/input/binding_profile.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/xr_action_set.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace evr::input {

// The touches on controls (not the thumb rest), in ButtonInput order from PrimaryTouch on.
inline constexpr std::array<ButtonInput, 5> kControlTouches{
    ButtonInput::PrimaryTouch,  ButtonInput::SecondaryTouch, ButtonInput::StickTouch,
    ButtonInput::TrackpadTouch, ButtonInput::TriggerTouch,
};
inline constexpr std::size_t kControlTouchCount = kControlTouches.size();

// True for a touch the thumb makes on the controller's face: A/X, B/Y, the stick, the trackpad.
[[nodiscard]] constexpr bool isThumbTouch(ButtonInput input) {
    return input >= ButtonInput::PrimaryTouch && input <= ButtonInput::TrackpadTouch;
}

// True for a touch on a control (PrimaryTouch .. TriggerTouch); false for the thumb rest and the switches.
[[nodiscard]] constexpr bool isControlTouch(ButtonInput input) {
    return input >= ButtonInput::PrimaryTouch && input <= ButtonInput::TriggerTouch;
}

// True for any touch sensor: the thumb rest or a touch on a control.
[[nodiscard]] constexpr bool isTouchInput(ButtonInput input) {
    return input == ButtonInput::ThumbRest || isControlTouch(input);
}

// The index of a control touch in kControlTouches.
[[nodiscard]] constexpr std::size_t controlTouchIndex(ButtonInput input) {
    return static_cast<std::size_t>(input) - static_cast<std::size_t>(ButtonInput::PrimaryTouch);
}

// The OpenXR action a touch input reads; nullopt for a switch.
std::optional<XrActionId> touchAction(ButtonInput input);

// The sensor's state this frame (false for a switch).
bool touchSensed(const HandState& hand, ButtonInput input);

// The touch's leaf under /user/hand/<hand> on the family's profile ("/input/a/touch"), or nullopt when the
// profile has no such sensor there (a profile we have no input list for is taken at its word).
std::optional<std::string> touchLeaf(const ControllerData& data, Hand hand, ButtonInput input);

// The controls under a touch, as a set of gameplay actions.
struct OwnControls {
    std::uint32_t actions = 0; // bit per XrActionId

    [[nodiscard]] bool has(XrActionId action) const { return (actions & bit(action)) != 0; }
    void add(XrActionId action) { actions |= bit(action); }
    [[nodiscard]] bool empty() const { return actions == 0; }

    friend bool operator==(const OwnControls&, const OwnControls&) = default;

private:
    static constexpr std::uint32_t bit(XrActionId action) { return 1u << static_cast<unsigned>(action); }
};
static_assert(kXrActionCount <= 32);

// The plain layout: for the thumb's touches the primary and secondary buttons, the stick and its click; for
// the trigger's touch the trigger.
OwnControls defaultOwnControls(ButtonInput touch);

// The controls under the touch itself, whose use voids a landing soon after (touch_button.hpp): from the
// family's data, the gameplay actions whose binding shares the touch's identifier ("/input/a" for
// "/input/a/touch"); where the family has no such touch, the plain layout's (the primary or secondary button,
// the stick and its click, nothing for the trackpad, the trigger).
OwnControls landingControlsOf(const ControllerData& data, Hand hand, ButtonInput touch);
OwnControls defaultLandingControls(ButtonInput touch);

// From the family's data: for the thumb's touches the gameplay actions bound on the controller's face (under
// the thumb, the same for all four); for the trigger's touch landingControlsOf (the trigger).
OwnControls ownControlsOf(const ControllerData& data, Hand hand, ButtonInput touch);

// What the mapper reads to tell an own control in use: the analog buttons as their hysteresis has them.
struct OwnControlLevels {
    bool trigger = false; // past the press threshold
    bool grip = false;
};

// The stick counts as used out past this (as the thumb-rest wheel's kArmDeadzone, rest_wheel.hpp).
inline constexpr float kOwnStickOut = 0.25f;

// True when one of `own` is in use this frame: a button down, the stick out past kOwnStickOut, the trigger
// or grip pressed.
bool ownControlUsed(const HandState& hand, OwnControls own, OwnControlLevels levels);

} // namespace evr::input
