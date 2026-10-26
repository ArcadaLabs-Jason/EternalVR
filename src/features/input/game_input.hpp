#pragma once

// What the mapper hands to the engine side each frame (the L1 input writer, or the L2 gamepad
// fallback, R13 section 5).

#include "features/input/axis2.hpp"
#include "features/input/rest_wheel_output.hpp"
#include "features/input/wrist_flick.hpp"
#include "game/eternal/game_action.hpp"

#include <array>

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
    // What the thumb-rest wheel did this frame (rest_wheel.hpp): whether it holds WeaponWheel (its pointer is
    // then the stick's, never the weapon hand's), its haptic ticks and what to log.
    RestWheelOutput restWheel;
    // One frame, when a trigger is pulled while the left Menu button is held (capture_chord.hpp): save the
    // next eye pair for a bug report (the layer's own, like the recenter; never sent to the game).
    bool capture = false;
    // Per hand (indexed by Hand): a physical punch this frame (punch_detector.hpp), for its vibration.
    std::array<bool, 2> punch{};
    // This frame's melee press is a punch's (no control held melee already): under hand aim it aims with the
    // head (action_aim.hpp).
    bool meleeFromPunch = false;
    // An overhead swing of the weapon hand (the Crucible) this frame (arm_gestures.hpp), or a jump with both
    // hands thrown up (hands_jump.hpp), for the log.
    bool swung = false;
    bool handsJumped = false;
    // A hand reached out and pressed use alone this frame (reach_use.hpp), for the log.
    bool reached = false;
    // The weapon hand's wrist rolled and switched the weapon mod this frame (wrist_flick.hpp), for the log
    // with the flick's rates.
    bool flicked = false;
    FlickOutput flick;
};

// Under hand aim this frame's melee-class press aims with the head (action_aim.hpp): a punch's melee, or a
// reach's use.
[[nodiscard]] inline bool pressAimsWithHead(const GameInput& input) {
    return input.meleeFromPunch || input.reached;
}

} // namespace evr::input
