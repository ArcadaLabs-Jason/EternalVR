#pragma once

// Gamepad mode in the game (features/input/gamepad_mode.hpp): the motion controllers off. installGameHooks
// puts its hooks in (controllers.cpp); the pad watch (xinput_hook.cpp) feeds notePadRead from the game's own
// pad reads; the camera hook (game_view_poses.cpp) and the forced-view gate (aim_hooks.cpp) ask
// gamepadView().
//
// Threads: notePadRead runs on the game's pad sampler, padMotion on the XR frame loop, gamepadView anywhere.

#include "features/input/virtual_gamepad.hpp"
#include "vkcore/controllers.hpp"

#include <optional>

namespace evr::vkcore::controllers {

// Replaces the exe's XInputGetState import with a watch that passes every call to XInput and hands XInput's
// answer back unchanged, noting each pad the game reads (xinput_hook.cpp). False (logged) when it cannot.
bool installPadWatch();

// The pad watch: the game read pad `user`; nullopt when XInput answered that it is not connected. Both sticks
// pressed count toward the recenter hold (noteRecenterBinding).
void notePadRead(unsigned user, const std::optional<input::PadState>& pad);

// The comfort vignette's motion in gamepad mode (artificialMotion): the move stick and the look stick's
// sideways push (pad_watch.hpp); zero when no pad was read for a moment.
ArtificialMotion padMotion();

// Gamepad mode's reads of the game view are in place (a known build): the camera hook runs the forced-view
// gate, bHaptics' frame and the piloted-demon check without the controllers attached. Set once by the
// install.
bool gamepadView();
void setGamepadView();

} // namespace evr::vkcore::controllers
