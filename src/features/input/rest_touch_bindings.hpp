#pragma once

// The touch bindings the thumb-rest weapon wheel reads (rest_wheel.hpp), and a thumb rest bound like a button
// (touch_button.hpp), which reads its rest the same way.
//
// A player's controller file replaces the built-in data of its profile whole (player_controller_data.hpp),
// so a file copied before the wheel existed has no thumb-rest binding. The layer adds the ones a family's
// data lacks before suggesting them; no file is rewritten. A runtime that refuses a profile for one of these
// paths refuses all of its bindings, so the layer then suggests the profile again: first without the sensors
// that only tell the thumb is elsewhere (withoutVetoTouch), keeping the rests, then without any of them
// (input_profiles.cpp).
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

#include "features/input/controller_bindings.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/xr_action_set.hpp"

#include <cstddef>

namespace evr::input {

struct RestTouchOptions {
    bool thumbRest = true; // the wheel is on, or a control map binds a thumb rest
    bool faceTouch = false;
};

// Whether one of `data`'s control maps binds a thumb rest to an action, in any press kind.
[[nodiscard]] bool bindsThumbRest(const ControllerData& data);

// The options for a family's data: its rests' bindings with the wheel on (`wheelOn`), or with a thumb rest
// bound in one of its maps; face-button touch as the rest (`faceTouch`) only with the wheel on.
RestTouchOptions restTouchOptions(const ControllerData& data, bool wheelOn, bool faceTouch);

// True for the actions this file adds.
bool isRestTouchAction(XrActionId action);

// Adds the rest-touch bindings `data` lacks (in the data's own order, by hand and action); a path another
// binding of the hand already uses is left alone. Returns how many were added.
std::size_t addRestTouchBindings(ControllerData& data, RestTouchOptions options);

// `data` without any rest-touch binding: the profile suggested again when the runtime refused it.
ControllerData withoutRestTouch(ControllerData data);

[[nodiscard]] bool hasRestTouchBindings(const ControllerData& data);

// `data` without the bindings that only tell the thumb is elsewhere (the stick's and the trackpad's touch,
// and the face buttons' touch beside a thumb rest), the rests kept: the first retry of a refused profile.
ControllerData withoutVetoTouch(ControllerData data);

[[nodiscard]] bool hasVetoTouchBindings(const ControllerData& data);

// Whether `hand` has a sensor the wheel reads as its thumb rest: the thumb rest, or with face touch one of
// its face buttons' touch.
bool handHasRest(const ControllerData& data, Hand hand, bool faceTouch);

} // namespace evr::input
