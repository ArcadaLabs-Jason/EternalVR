#pragma once

// What the mapper hands to the engine side each frame (the L1 input writer, or the L2 gamepad
// fallback, R13 section 5).

#include "features/input/axis2.hpp"
#include "game/eternal/game_action.hpp"

namespace evr::input {

struct GameInput {
    game::GameActionSet down;
    game::GameActionSet pressed;  // Went down this frame.
    game::GameActionSet released; // Went up this frame.

    // Movement relative to the game's view yaw: +y forward, +x right, magnitude at most 1.
    Axis2 move;
    // Yaw to add this frame, in degrees; positive turns left (counter-clockwise seen from above).
    float turnDegrees = 0.0f;
    // Weapon wheel selection direction while the wheel is held open, zero otherwise.
    Axis2 wheelPointer;
    // One frame, when a trigger is pulled while the left Menu button is held (capture_chord.hpp): save the
    // next eye pair for a bug report (the layer's own, like the recenter; never sent to the game).
    bool capture = false;
};

} // namespace evr::input
