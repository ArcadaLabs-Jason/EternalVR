#pragma once

// Where the hand-aim dot goes along the weapon hand's ray (docs/VR_CONTROLLERS.md, the aim dot). A dot at a
// fixed distance lines up with the shots only at that distance: the eyes sit about 0.3 m from the hand, so
// at 10 m a target 50 m away is about 1.4 degrees off the dot (a player's report with the Precision Bolt's
// scope). The camera hook traces the ray through the world with the head sweep's collision query
// (head_sweep.hpp) and the dot sits where it hits, at the same angular size.

#include "common/vector.hpp"

#include <cstddef>

namespace evr::vkcore {

// How far the trace reaches (metres); a clear ray puts the dot this far away.
inline constexpr float kReticleReachMetres = 100.0f;
// The nearest the dot comes (metres), so it never sits inside the gun.
inline constexpr float kReticleNearestMetres = 0.3f;

// Camera hook, after controllers::endGameView: the metres along the weapon hand's ray to the first world
// surface, kReticleReachMetres when the ray is clear, or 0 when there is no ray or no collision query.
// `eye` is the game's view origin before the head's offset; `player` is ignored by the trace.
float reticleHitMetres(const std::byte* player, Vec3 eye, float unitsPerMetre);

} // namespace evr::vkcore
