#pragma once

// The eye separation the game renders with (ETERNALVR_IPD). The runtime's eye positions are kept unless
// the player sets their own: then the two eyes move apart or together along the line joining them, about
// their midpoint, to the requested distance. Only the game's rendering uses it; the compositor is still
// given the runtime's eye poses.

#include "common/vector.hpp"

#include <array>

namespace evr::roomscale {

// Eye positions (head frame, metres) `ipdMetres` apart with the same midpoint and direction. Unchanged
// when `ipdMetres` is not in [0.04, 0.09] or the eyes are less than 1 cm apart.
std::array<Vec3, 2> withSeparation(const std::array<Vec3, 2>& eyes, float ipdMetres);

} // namespace evr::roomscale
