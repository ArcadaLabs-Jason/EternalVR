#pragma once

// Where the light scattering filter keeps its state in the engine's struct
// (docs/rig-findings/stereo-scatter.md; stereo_seq/scatter_history.hpp keeps a copy of it per eye).

#include <cstddef>

namespace evr::engine {

// The filter's state: the struct from +0x60 to +0x94.
inline constexpr std::size_t kScatterStateOffset = 0x60;
inline constexpr std::size_t kScatterStateBytes = 0x34;
// Within the state: the last frame the volume was computed for (+0x70 in the struct).
inline constexpr std::size_t kScatterLastFrame = 0x70 - kScatterStateOffset;

} // namespace evr::engine
