#pragma once

// The head sweep (room-scale v1, T-062): the engine's own collision query, for where a sphere moving from
// the game's eye to the rendered head first touches the world, and for the aim dot's and a cutscene's rays.
//
// Installed once while the multiplayer guard is armed; every call asks the guard again. Without the
// query (an unknown build, a signature that does not match, ETERNALVR_HEAD_COLLISION=0) the head offset
// keeps its lean cap and never fades: the conservative fallback.
//
// When the world may be queried (features/roomscale/late_trace.hpp): only at rest, never while the game's
// physics step is in flight. The camera hook usually runs while it is: its sweeps are then asked there, made
// in the next game frame just after the game syncs the step, and answered one game frame late. With no step
// in flight (a pause, the step synced in the same frame) it sweeps at once.

#include "common/vector.hpp"
#include "features/roomscale/late_trace.hpp"

#include <cstddef>
#include <optional>

namespace evr::vkcore {

// Channels of the camera hook's shot-shaped sweeps: the aim dot's ray, then a cutscene's fan of rays
// (xr_math::cinemaRayFan); the head has its own.
using roomscale::kLateCinemaFirst;
using roomscale::kLateCinemaRays;
using roomscale::kLateReticle;

// XR worker, head-tracked mode, guard armed: resolves the query in the game's code and hooks the end of
// the physics step's post-step job; logs what it found.
bool installHeadSweep();

// True once installHeadSweep found the query and hooked the end of the post-step job.
bool headSweepAvailable();

// Camera hook only. The sweep of the world's 0.16 m sphere (clip16sphere, the size the engine keeps) from
// `from` to `to` (world, game units) against the world, ignoring `player`: made at once with the world at
// rest, else asked for and the answer to the one asked a game frame before returned. The fraction of the way
// at the first contact, or nullopt when the way was clear, there is no answer, the answer's sweep started
// more than `fromSlack` (game units) from `from` (a teleport), or the query is unavailable or failed (never
// throws; a fault inside the game's code is caught and turns the query off).
std::optional<float> sweepHead(const std::byte* player, Vec3 from, Vec3 to, float fromSlack);

// The sweep with the weapon trace's shape and contents (monsters in, player clip out): what lies between the
// eye and a shot's start at the hand. The fire hook only (inside the game's frame update, where the game
// makes the same query): swept at once; with the world not at rest (not expected there) nullopt, logged and
// counted. `startedInContact` is set when `from` is already touching something (nullopt is returned).
std::optional<float> sweepShot(const std::byte* player, Vec3 from, Vec3 to, bool& startedInContact);

// The camera hook's shot-shaped sweep on `channel` (kLateReticle, kLateCinemaFirst + ray), made or asked as
// sweepHead's; `startedInContact` as for sweepShot.
std::optional<float> sweepShotLate(std::size_t channel,
                                   const std::byte* player,
                                   Vec3 from,
                                   Vec3 to,
                                   float fromSlack,
                                   bool& startedInContact);

// Camera hook: drops what `count` channels from `first` asked or were answered (a cutscene's camera cut).
void forgetLateSweeps(std::size_t first, std::size_t count);

} // namespace evr::vkcore
