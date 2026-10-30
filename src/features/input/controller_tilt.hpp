#pragma once

// Controller angle (ETERNALVR_WEAPON_HAND_TILT, ETERNALVR_OFF_HAND_TILT; docs/VR_CONTROLLERS.md "Controller
// angle"): each controller turned by a fixed angle about its sideways axis, for a player whose hands sit at
// another angle on the controller than the mod assumes (another controller family, a strap, a grip of their
// own). The layer gives the turn to the runtime as the pose in action space of the controller's aim and grip
// spaces (vkcore/input_xr.cpp createSpaces), as a SteamVR device offset turns the controller, so everything
// placed from them follows: the gun, the shots, the aim dot, the menu pointer, the glove and the hand HUD.
// Each pose turns about its own x axis (the controller's right; the grip's and the aim's are the same
// direction) through its own origin. A positive angle lifts the controller's front, a negative one lowers it
// (the sign of SteamVR's pitch offset). Scripted rig poses, which no space gives, take the same turn here.
//
// Pure: no OpenXR.

#include "common/pose.hpp"
#include "features/input/controller_settings.hpp"
#include "features/input/controller_state.hpp"

namespace evr::input {

inline constexpr float kMaxControllerTiltDegrees = 60.0f;

// The turn of `degrees` about x, as a pose in the controller's own space; identity for 0 or a non-finite
// angle.
Pose controllerTilt(float degrees);

// `h`'s grip and aim poses each turned by controllerTilt(degrees) in their own space, as the runtime turns
// them. Velocities are left as they are.
void tiltController(HandState& h, float degrees);

// The angle `settings` gives `hand`'s controller: the weapon hand's or the other's.
float controllerTiltDegrees(const ControllerSettings& settings, Hand hand, Hand weaponHand);

// Both controllers of `frame` turned by the angles `settings` gives them.
void tiltControllers(InputFrame& frame, Hand weaponHand, const ControllerSettings& settings);

} // namespace evr::input
