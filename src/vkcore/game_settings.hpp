#pragma once

// The game's own video settings in the layer log, so a player's frame rate can be read against them (ray
// tracing, DLSS, resolution scaling, the Advanced quality settings, field of view):
//
//   game settings: r_enableRayTracing 1, r_raytracedReflections 1, ..., g_fov 110
//
// The cvars are the ones game_settings_line.hpp lists, read where the engine keeps their values (the values
// block: +0x08 the integer, +0x0C the float; docs/rig-findings/perf-cpu-cvars.md) through a guarded copy. The
// line is logged a few seconds after every map load (mp_guard::mapLoads) and again whenever a value changes
// while the player stays in the map (checked every 10 seconds). A cvar this build does not register once is
// named once and left out of the line. Only reads, each one guarded; nothing in the game is written. Only
// while the multiplayer guard allows touching the game (mp_guard::allowsGameTouch): before it arms and after
// a trip nothing is read and no line is logged. The launcher's Export report copies the last line of the
// newest session into system.txt.

namespace evr::vkcore::game_settings {

// Reads the cvars and logs the line when it is due. Called on every present; cheap until a read is due, and
// the first read locates the cvars (one scan of the game's code).
void poll();

} // namespace evr::vkcore::game_settings
