#pragma once

// The body yaw turning more than 90 degrees in one game frame (a stick turn never does): logged with what
// drove the view, to find the rare half turn seen once in a seated glory kill (2026-09-28). Camera hook
// thread only.
//
// And the player's teleports (comfort::TeleportWatch): `game: teleport N: the player moved X m in one game
// frame (...); the view turned Y deg (...)` (the first 40), whatever the settings, from the player physics'
// origin and velocity (playerPhysicsPlace, game_motion.hpp). Logging only.

#include <cstddef>

namespace evr::vkcore {

// Each head-tracked game view, with the body yaw the view was built on and what drove it.
void watchBodyYaw(float yaw, bool headAimed, bool forcedView, bool cutscene, bool menuUp);

// Each head-tracked game view, after watchBodyYaw: the player, the view's time (seconds, a steady clock) and
// the same body yaw and flags.
void watchTeleport(const std::byte* player, double seconds, float yaw, bool forcedView, bool cutscene);

} // namespace evr::vkcore
