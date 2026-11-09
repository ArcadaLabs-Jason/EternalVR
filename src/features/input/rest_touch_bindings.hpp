#pragma once

// The touch bindings the thumb-rest weapon wheel reads (rest_wheel.hpp), and a thumb rest bound like a button
// (touch_button.hpp), which reads its rest the same way.
//
// A player's controller file replaces the built-in data of its profile whole (player_controller_data.hpp),
// so a file copied before the wheel existed has no thumb-rest binding. The layer adds the ones a family's
// data lacks before suggesting them; no file is rewritten. A runtime that refuses a profile for one of these
// paths refuses all of its bindings, so the layer then suggests the profile again (touchRetries,
// input_profiles.cpp): first without the sensors that only tell the thumb is elsewhere (withoutVetoTouch),
// keeping the rests and the touches a map binds like buttons; then without those touches too
// (withoutTouchButtons), keeping the rests; then without any of them.
//
// - The thumb rest: /input/thumbrest/touch on each hand whose profile lists it (only Touch controllers have
//   the sensor; Touch Pro and Touch Plus report as Touch).
// - Face-button touch (ETERNALVR_THUMBREST_FACE_TOUCH, off by default): the touch leaf of each hand's primary
//   and secondary buttons (A/B, X/Y), for controllers without a thumb rest (Index, Pico 4). Not on the Steam
//   Frame, whose bumper already holds the wheel, and not as the rest on Touch, which has the real sensor.
// - The thumb elsewhere (DECISIONS T-120): on a hand with a thumb rest, the same face-button touch leaves;
//   on any hand with a rest, the stick's touch (/input/thumbstick/touch); beside face touch, the trackpad's
//   (/input/trackpad/touch, Index); where the profile lists them. A touch of the rest begun with the thumb on
//   these waits, and one lost after the thumb went to them lifts at once (rest_wheel.hpp). They are not rests
//   themselves (handHasRest).
// Nothing is added on the Steam Frame, nor with the wheel off unless one of the family's control maps binds
// a thumb rest (restTouchOptions): the built-in maps bind none.
//
// A touch on a control bound like a button (touch_inputs.hpp, DECISIONS T-126) gets its own binding the same
// way (addBoundTouchBindings), on any family whose profile has the sensor, the Steam Frame too, and only
// when one of the family's maps binds it: the built-in maps bind none, so nothing is added for them. The
// first retry of a refused profile keeps such a binding (it is not only the thumb elsewhere); the second
// drops it and keeps the rests, so a touch the runtime will not bind costs the wheel nothing.
//
// The trigger's touch for the fingers (addTriggerTouchBindings): /input/trigger/touch on each hand whose
// profile lists it (Touch, Index, Pico 4, Steam Frame; not the Vive wands, the Cosmos, the Reverb G2 or
// Windows Mixed Reality), on every family whatever its maps bind, so the off hand's index finger can rest on
// its trigger and the weapon hand's lift off it (features/arm/hand_pose.hpp, trigger_finger.hpp). The
// built-in data suggests no binding for it, and before these fingers nothing read it unless a map bound it
// like a button.
//
// The grip's curl for the fingers (addGripCurlBindings, DECISIONS T-226): /input/squeeze/value as the
// grip_curl action on each hand whose profile lists it and whose grip action reads another input of the
// squeeze. In the built-in data that is the Index alone: its grip action reads /input/squeeze/force, so a
// hand resting round the handle presses nothing, and SteamVR's squeeze value there rises as the fingers wrap
// the handle, before any force. The off hand's fingers close with it, so they follow the hand without the
// squeeze that fires the grip's action (the Flame Belch). Where the grip action itself reads the value
// (Touch, the Reverb G2, Pico 4, the Steam Frame) the path is that binding's and nothing is added: the
// fingers read the grip. A controller without the value (Windows Mixed Reality, the Vive wands, the Cosmos)
// gets nothing.
//
// The first retry of a refused profile drops both of the fingers' bindings, and only them
// (withoutFingerBindings), so a runtime that refuses one of those paths costs the fingers that sensor and
// keeps the thumb-rest wheel; every later retry is without them too.

#include "features/input/binding_profile.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/xr_action_set.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace evr::input {

struct RestTouchOptions {
    bool thumbRest = true; // the wheel is on, or a control map binds a thumb rest
    bool faceTouch = false;
};

// Whether one of `data`'s control maps binds a thumb rest to an action, in any press kind.
[[nodiscard]] bool bindsThumbRest(const ControllerData& data);

// Whether one of `data`'s control maps binds `touch` of `hand` to an action, in any press kind.
[[nodiscard]] bool bindsTouch(const ControllerData& data, Hand hand, ButtonInput touch);

// The options for a family's data: its rests' bindings with the wheel on (`wheelOn`), or with a thumb rest
// bound in one of its maps; face-button touch as the rest (`faceTouch`) only with the wheel on.
RestTouchOptions restTouchOptions(const ControllerData& data, bool wheelOn, bool faceTouch);

// True for the touch actions this file adds (not the grip's curl, which is no touch).
bool isRestTouchAction(XrActionId action);

// Adds the rest-touch bindings `data` lacks (in the data's own order, by hand and action); a path another
// binding of the hand already uses is left alone. Returns how many were added.
std::size_t addRestTouchBindings(ControllerData& data, RestTouchOptions options);

// Adds the binding of each touch on a control (isControlTouch) a map of `data` binds and the data lacks,
// where the profile has the sensor and no other binding of the hand uses its path. Returns how many.
std::size_t addBoundTouchBindings(ControllerData& data);

// Adds the trigger's touch binding on each hand of `data` whose profile has the sensor, where the data lacks
// it and no other binding of the hand uses its path. Returns how many were added.
std::size_t addTriggerTouchBindings(ControllerData& data);

// Adds the grip's curl (/input/squeeze/value) on each hand of `data` whose profile has it, where the data
// lacks it and no other binding of the hand uses its path (on every built-in family but the Index the grip
// action does). Returns how many were added.
std::size_t addGripCurlBindings(ControllerData& data);

// `data` without any rest-touch binding: the profile suggested again when the runtime refused it.
ControllerData withoutRestTouch(ControllerData data);

[[nodiscard]] bool hasRestTouchBindings(const ControllerData& data);

// `data` without the bindings that only tell the thumb is elsewhere (the stick's and the trackpad's touch,
// and the face buttons' touch beside a thumb rest), the rests and the touches a map binds like buttons kept:
// the first retry of a refused profile.
ControllerData withoutVetoTouch(ControllerData data);

[[nodiscard]] bool hasVetoTouchBindings(const ControllerData& data);

// `data` without the veto sensors' bindings and those of the touches on controls a map binds like buttons,
// the rests kept: the second retry of a refused profile.
ControllerData withoutTouchButtons(ControllerData data);

// `data` without the bindings only the fingers read: the trigger's touch (addTriggerTouchBindings), except
// where a map binds it like a button, and the grip's curl (addGripCurlBindings). The first retry of a refused
// profile, so a runtime that refuses only one of those paths costs the fingers that sensor and nothing else.
ControllerData withoutFingerBindings(ControllerData data);

// Whether `data` suggests a touch on a control that one of its maps binds like a button.
[[nodiscard]] bool hasTouchButtonBindings(const ControllerData& data);

// The retries of a refused profile, in order (rest_touch_bindings.hpp above), each without what the one
// before left out.
enum class TouchRetry : std::uint8_t {
    WithoutFingers,      // without the fingers' trigger touch and grip curl alone: everything else kept
    WithoutVeto,         // ... and without the sensors that only tell the thumb is elsewhere
    WithoutTouchButtons, // ... and without the touches bound like buttons: the rests kept
    WithoutRestTouch,    // without any of the bindings this file adds
};

struct TouchRetryStep {
    TouchRetry retry = TouchRetry::WithoutVeto;
    ControllerData data; // what is suggested
};

// The retries for `data`, in order, each only where it leaves out more than the one before (none when `data`
// has none of these bindings).
std::vector<TouchRetryStep> touchRetries(const ControllerData& data);

// Whether `hand` has a sensor the wheel reads as its thumb rest: the thumb rest, or with face touch one of
// its face buttons' touch.
bool handHasRest(const ControllerData& data, Hand hand, bool faceTouch);

// Whether a face button's touch can be the wheel's rest on `data`'s family: Face buttons as thumb rest on
// (`faceTouch`), and not the Steam Frame, whose bumper holds the wheel (a face touch a map binds there gets
// its binding, but it is only a touch bound as a button).
[[nodiscard]] bool faceTouchRests(const ControllerData& data, bool faceTouch);

// The hands, by Hand, with a sensor the wheel reads as a rest on `data`'s family: handHasRest with
// faceTouchRests.
[[nodiscard]] std::array<bool, 2> wheelRestHands(const ControllerData& data, bool faceTouch);

} // namespace evr::input
