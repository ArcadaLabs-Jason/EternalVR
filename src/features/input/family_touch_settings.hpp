#pragma once

// The mapper's settings that come from the controller family's data (vkcore/control_map.cpp builds the
// mapper with them): which hands have a rest the thumb-rest wheel reads (rest_touch_bindings.hpp), whether a
// face button's touch counts as the rest there (never on the Steam Frame, even where a map binds that touch
// and so has its binding), which hands rest on a real thumb rest, and the controls under each touch on a
// control (touch_inputs.hpp). Pure.

#include "features/input/controller_bindings.hpp"
#include "features/input/input_mapper.hpp"

namespace evr::input {

// Fills those settings of `settings` from `data`. `faceTouch`: the player's Face buttons as thumb rest;
// `scripted`: scripted input (ETERNALVR_TEST_INPUT) stands in for any controller, so both hands have a rest.
void applyFamilyTouchSettings(MapperSettings& settings,
                              const ControllerData& data,
                              bool faceTouch,
                              bool scripted);

} // namespace evr::input
