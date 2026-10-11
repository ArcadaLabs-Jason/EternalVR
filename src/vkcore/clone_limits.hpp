#pragma once

// Parallel Eye Rendering: how many clone builds a process makes (view_clones.cpp), and so how many of view
// 1's stencil views (clone_stencil.hpp, one per build) can wait for the engine's resize. Pure (unit tests).

#include "vkcore/parallel_eyes_settings.hpp"

namespace evr::vkcore::clone_limits {

// Changes of the engine's targets before the clones turn off (the first set counts as one). With stable names
// a rebuild leaves only its render target objects behind (about 40 KB of CPU memory); with a new set of names
// each build (ETERNALVR_TEST_VIEW_CLONE_NAMES=build) it left the whole old set allocated until a map load.
inline constexpr int kMaxBuilds = 32;
inline constexpr int kMaxBuildsNewNames = 8;
// Builds after the engine freed a clone (a map load), not counted against kMaxBuilds; one more turns the
// clones off.
inline constexpr int kMaxRemakes = 64;

// The most builds a process makes: kMaxBuilds (or kMaxBuildsNewNames), kMaxRemakes and the rebuilds
// ETERNALVR_TEST_VIEW_CLONE_REBUILD asks for (parallel_eyes::kForcedRebuilds), neither counted against the
// first.
inline constexpr int kMaxBuildsPerProcess = kMaxBuilds + kMaxRemakes + parallel_eyes::kForcedRebuilds;
inline constexpr int kMaxBuildsPerProcessNewNames =
    kMaxBuildsNewNames + kMaxRemakes + parallel_eyes::kForcedRebuilds;

} // namespace evr::vkcore::clone_limits
