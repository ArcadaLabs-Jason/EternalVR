#pragma once

// The touch bindings the thumb-rest weapon wheel reads (rest_wheel.hpp).
//
// A player's controller file replaces the built-in data of its profile whole (player_controller_data.hpp),
// so a file copied before the wheel existed has no thumb-rest binding. The layer adds the ones a family's
// data lacks before suggesting them; no file is rewritten. A runtime that refuses a profile for one of these
// paths refuses all of its bindings, so the layer then suggests the profile again without them
// (input_profiles.cpp).
//
// - The thumb rest: /input/thumbrest/touch on each hand whose profile lists it (only Touch controllers have
//   the sensor; Touch Pro and Touch Plus report as Touch).
// - Face-button touch (ETERNALVR_THUMBREST_FACE_TOUCH, off by default): the touch leaf of each hand's primary
//   and secondary buttons (A/B, X/Y), for controllers without a thumb rest (Index, Pico 4). Not on the Steam
//   Frame, whose bumper already holds the wheel, and not on Touch, which has the real sensor.

#include "features/input/controller_bindings.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/xr_action_set.hpp"

#include <cstddef>

namespace evr::input {

struct RestTouchOptions {
    bool thumbRest = true; // the wheel is on
    bool faceTouch = false;
};

// True for the actions this file adds.
bool isRestTouchAction(XrActionId action);

// Adds the rest-touch bindings `data` lacks (in the data's own order, by hand and action); a path another
// binding of the hand already uses is left alone. Returns how many were added.
std::size_t addRestTouchBindings(ControllerData& data, RestTouchOptions options);

// `data` without any rest-touch binding: the profile suggested again when the runtime refused it.
ControllerData withoutRestTouch(ControllerData data);

[[nodiscard]] bool hasRestTouchBindings(const ControllerData& data);

// Whether `hand` has a sensor the wheel reads as its thumb rest: the thumb rest, or with face touch one of
// its face buttons' touch.
bool handHasRest(const ControllerData& data, Hand hand, bool faceTouch);

} // namespace evr::input
