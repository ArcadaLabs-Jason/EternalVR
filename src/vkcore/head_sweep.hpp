#pragma once

// The head sweep (room-scale v1, T-062): the engine's own collision query, called from the camera hook
// to find where a sphere moving from the game's eye to the rendered head first touches the world.
//
// Installed once while the multiplayer guard is armed; every call asks the guard again. Without the
// query (an unknown build, a signature that does not match, ETERNALVR_HEAD_COLLISION=0) the head offset
// keeps its lean cap and never fades: the conservative fallback.

#include "common/vector.hpp"

#include <cstddef>
#include <optional>

namespace evr::vkcore {

// XR worker, head-tracked mode, guard armed: resolves the query in the game's code; logs what it found.
bool installHeadSweep();

// True once installHeadSweep found the query.
bool headSweepAvailable();

// Camera hook (game-frame thread) only. Sweeps a sphere of `radius` (game units) from `from` to `to`
// (world, game units) against the world, ignoring `player`. Returns the fraction of the way at the first
// contact, or nullopt when the way is clear, the query is unavailable, or it failed (never throws; a
// fault inside the game's code is caught and turns the query off).
std::optional<float> sweepHead(const std::byte* player, Vec3 from, Vec3 to, float radius);

} // namespace evr::vkcore
