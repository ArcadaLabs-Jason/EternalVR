#pragma once

// Controller angle (ETERNALVR_WEAPON_HAND_TILT, ETERNALVR_OFF_HAND_TILT; docs/VR_CONTROLLERS.md "Controller
// angle"): each controller turned by a fixed angle about its sideways axis, for a player whose hands sit at
// another angle on the controller than the mod assumes (a strap, a grip of their own). The angle is on top of
// the controller family's own fit (controller_fit.hpp: Valve Index's), which moves and turns the poses the
// same way. The layer gives both to the runtime as the pose in action space of the controller's aim and grip
// spaces (vkcore/input_spaces.cpp), as a SteamVR device offset moves the controller, so everything placed
// from them follows: the gun, the shots, the aim dot, the menu pointer, the glove and the hand HUD.
// Each pose turns about its own x axis (the controller's right; the grip's and the aim's are the same
// direction) through its own origin. A positive angle lifts the controller's front, a negative one lowers it
// (the sign of SteamVR's pitch offset). Scripted rig poses, which no space gives, take the same here.
//
// Pure: no OpenXR.

#include "common/pose.hpp"
#include "features/input/controller_fit.hpp"
#include "features/input/controller_settings.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/test_input.hpp"

#include <array>
#include <string>
#include <string_view>

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

// `hand`'s whole angle: the fit's for its role plus the player's.
float handTiltDegrees(const ControllerFit& fit,
                      const ControllerSettings& settings,
                      Hand hand,
                      Hand weaponHand);

// The pose in action space of `hand`'s grip space and of its aim space: the fit's shift in that pose's frame,
// then handTiltDegrees about the pose's own x axis through the shifted origin.
struct HandSpaceOffsets {
    Pose grip;
    Pose aim;
};
HandSpaceOffsets
handSpaceOffsets(const ControllerFit& fit, const ControllerSettings& settings, Hand hand, Hand weaponHand);

// A scripted hand's pose (its grip given at its aim's pose) as the runtime would give it: the aim's offset,
// for both, so the grip stays at the aim's pose.
Pose scriptedHandPose(const Pose& pose,
                      const ControllerFit& fit,
                      const ControllerSettings& settings,
                      Hand hand,
                      Hand weaponHand);

// The hands of `frame` that `script` poses (scriptPosesHand) through scriptedHandPose, each with its own fit
// (`fits`, by Hand). A hand the runtime gave already has the fit and the angle from its spaces and is left as
// it is, so a rig run with scripted input shows what a player gets. Velocities are left as they are.
void fitScriptedHands(InputFrame& frame,
                      const TestInput& script,
                      Hand weaponHand,
                      const ControllerSettings& settings,
                      const std::array<ControllerFit, 2>& fits);

// The log's line for `hand`'s controller of the family named `familyName`: the fit it has (`fit`; with none
// and `familyHasFit`, the family has one on SteamVR only) and its whole angle with the player's, without a
// tag.
std::string fitText(Hand hand,
                    std::string_view familyName,
                    const ControllerFit& fit,
                    bool familyHasFit,
                    const ControllerSettings& settings,
                    Hand weaponHand);

} // namespace evr::input
