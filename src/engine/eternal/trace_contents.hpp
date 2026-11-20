#pragma once

// The contents masks the layer's collision sweeps ask the game's query for
// (docs/rig-findings/collision-query.md; features/roomscale/late_trace.hpp).

#include <cstdint>

namespace evr::engine {

// The head's sweep: the world and player clip (MASK_PLAYERDEADSOLID, no monsters).
inline constexpr std::int32_t kHeadTraceContents = 0x100009;
// The weapon rays: what a shot hits (the weapon trace's contents, monsters in, player clip out).
inline constexpr std::int32_t kShotTraceContents = 0x140001;

} // namespace evr::engine
