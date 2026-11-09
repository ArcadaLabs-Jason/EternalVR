#pragma once

// A controller family's own fit (docs/VR_CONTROLLERS.md "Controller fit"): how its grip and aim poses are
// moved and turned so the gun and the glove sit in the hand as they do on the controllers the hands were
// tuned with (Quest Touch). The runtime applies it, with the player's controller angle on top, as the pose in
// action space of the hand's spaces (controller_tilt.hpp handSpaceOffsets, vkcore/input_spaces.cpp), so
// everything placed from them follows, as with the angle alone.
//
// Valve Index, from one player's setup (public issue #16, 0.1.87, SteamVR, a Bigscreen Beyond): the
// launcher's angles at -20 (weapon hand) and -40 (off hand), and SteamVR's raw device offsets for the game at
// 0.06 m along the device's +Y and 0.06 m along its -Z on both controllers, with no rotation. Those offsets
// move the controller's raw (tracking) pose in its own frame, and SteamVR fixes the OpenXR poses in that
// frame: the Index's render model (valve_controller_knu_1_0) has no openxr_grip or openxr_aim component, so
// the grip is its "grip" (turned 15.392 degrees about the raw frame's x) and the aim its "tip" (turned -40
// degrees); the player's log has the grip 55.3 degrees from the aim, their difference. The same move is the
// offset turned into each pose's frame: (0, 4.2, -7.4) cm in the grip's and (0, 8.5, -0.7) cm in the aim's,
// one move in the room, so the gun (at the grip) and the shots and the aim dot (from the aim) move together.
// The poses' small yaw and roll in the render model (2 and 5 degrees, mirrored per hand) are left out: under
// 5 mm.
//
// Each hand takes the fit of its own controller's family, from the profile the runtime reports for that hand,
// and only on SteamVR, whose Index poses the numbers come from (runtimeHasFits).
//
// Pure: no OpenXR.

#include "common/vector.hpp"
#include "game/eternal/controller_data.hpp"

#include <optional>
#include <string_view>

namespace evr::input {

struct ControllerFit {
    // Each pose turned about its own x axis through its own origin, by role, degrees: + lifts the front (the
    // player's angle adds to it).
    float weaponHandTiltDegrees = 0.0f;
    float offHandTiltDegrees = 0.0f;
    // Both controllers moved by one vector in the room, given in the grip's frame and in the aim's, metres.
    Vec3 gripShift;
    Vec3 aimShift;

    friend bool operator==(const ControllerFit&, const ControllerFit&) = default;
};

// The fit of `family`: Valve Index's, and none (all zero) for every other family.
ControllerFit controllerFit(game::Controller family);

// True on a runtime whose name says it is SteamVR (ETERNALVR_TEST_RUNTIME_NAME stands in on the rig): the
// Index fit is worked out from SteamVR's own Index poses, and another runtime defines its Index grip and aim
// itself.
bool runtimeHasFits(std::string_view runtimeName);

// controllerFit(family) on SteamVR; none on any other runtime.
ControllerFit controllerFit(game::Controller family, std::string_view runtimeName);

// The retries of a hand's spaces made again for its family's fit (vkcore/input_spaces.cpp) after the runtime
// refused them: at most kRefitAttempts tries for a family, kRefitRetrySeconds apart, so a runtime that keeps
// refusing costs a few log lines and nothing per frame. Another family starts over.
inline constexpr int kRefitAttempts = 5;
inline constexpr double kRefitRetrySeconds = 3.0;

class RefitRetry {
public:
    // True when a try for `family` may be made at `seconds` (the caller's clock); counts it.
    bool take(game::Controller family, double seconds);
    // True once the tries for `family` are used up.
    [[nodiscard]] bool exhausted(game::Controller family) const;

private:
    std::optional<game::Controller> family_;
    int attempts_ = 0;
    double lastSeconds_ = 0.0;
};

} // namespace evr::input
