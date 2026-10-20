#pragma once

// What the user-command hook writes into the game's command (docs/VR_CONTROLLERS.md "User command",
// docs/VR_ROOMSCALE.md "Body follow").
//
// Two features write there: the motion controllers (their buttons and move) and room-scale body follow (its
// move toward the head). The controllers' input goes through the command unless they are off (gamepad mode)
// or feed the virtual gamepad instead. Body follow needs the command in every mode: without the controllers'
// writes the hook is still installed, for body follow alone, and the game's own pad, keyboard and mouse
// input stays in the command exactly as the game built it.

#include "features/input/controller_settings.hpp"

#include <cstdint>

namespace evr::input {

enum class CommandHookUse : std::uint8_t {
    None,        // no hook: nothing writes into the command
    Controllers, // the controllers' buttons and move, then body follow's move
    BodyFollow,  // body follow's move only
};

// `controllers`: the motion controllers are on (ETERNALVR_CONTROLLERS); `path`: their input path
// (ETERNALVR_XINPUT); `bodyFollow`: room-scale body follow is on (ETERNALVR_BODY_FOLLOW).
CommandHookUse commandHookUse(bool controllers, InputPath path, bool bodyFollow);

} // namespace evr::input
