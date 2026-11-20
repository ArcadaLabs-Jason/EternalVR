#pragma once

// Motion the game makes, for the comfort vignette (features/comfort/launch_pulse.hpp, ride.hpp,
// vignette.hpp): the launches the launch hooks see (bhaptics_launch.cpp: jump pads, boosters), and the
// player physics read once a game view (game_motion.cpp): whether the feet are on the ground, which ends a
// launch's hold, the ground pound, the Sentinel Hammer's slam [inferred], and the body's place and own
// velocity, whose difference tells a ride (a tram, an elevator, a moving platform: comfort::CarriedSpeed).
// Offsets from the type-info tables of Steam build 25216728 (idPlayer::physicsObjHavok at +0x8A50, an
// idHavokPhysics_Player); reads through SEH only, nothing is written. The launch hooks go in only with the
// vignette on (or bHaptics), so a player with both off pays for no hook, and the frame reads run only with
// the vignette on (or the rig's trace): with it off nothing is read or logged for rides and launches.
//
// Log: `game: ride N starts: ...` and `game: ride N ends after X s (<why>); fastest Y m/s` (the first 40);
// `vignette: launch N (...) held X s: ...` (the first 40). ETERNALVR_GAME_MOTION_TRACE=1 (rig) adds the
// physics' readings every 0.25 s while anything moves (`game motion trace: ...`, the first 600).

#include "features/comfort/launch_pulse.hpp"

#include <cstddef>
#include <string>

namespace evr::vkcore::controllers {

// ETERNALVR_VIGNETTE is light or strong (the presenter's own reading of it, once).
bool vignetteOn();

// The controllers' install and gamepad mode's, with the build known (PlayerAim): with the vignette on, the
// launch hooks (installLaunchHooks), logged either way.
void installGameMotionHooks();

// The launch hooks, once a launch (already filtered, bhaptics::LaunchFilter): `what` and `name` for the log.
void noteGameLaunch(const char* what, const std::string& name, const comfort::LaunchSize& size);

// Camera hook, every head-tracked game view, with the controllers or in gamepad mode: reads the player
// physics for the launch's ground, the ground pound and the rides. `cutscene` (the render view's flag) and
// `wallClimb` (climbFrame) are never rides.
void noteGameMotionFrame(const std::byte* player, bool cutscene, bool wallClimb);

// XR worker, every frame with the vignette on: the launch pulse's amount now and the ride's (0 to 1 each;
// the ride's 0 once no game frame came for a moment).
struct GameMotionVignette {
    float launch = 0.0f;
    float ride = 0.0f;
};
GameMotionVignette gameMotionVignette();

} // namespace evr::vkcore::controllers
