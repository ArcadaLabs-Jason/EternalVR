#pragma once

// ETERNALVR_DEBUG_CVARS (rig experiments; runtime_cvars.hpp writes its entries) against per-eye TAA's own
// writes (taa_hooks.cpp), which run on every stereo tick: an entry wins, so per-eye TAA leaves the cvars the
// list holds alone (stereo_seq/debug_cvars.hpp). The list is read once, on the first call.

#include <string_view>

namespace evr::vkcore {

// From the per-eye TAA hooks' install: the cvars the list holds, logged once when it holds any.
void logTaaDebugCvars();

// The list holds `name`, so per-eye TAA does not write it; logged the first time for each cvar
// ("<name> left to ETERNALVR_DEBUG_CVARS").
bool taaLeftToDebugCvars(std::string_view name);

// The list holds r_SSR or r_raytracedReflectionsTemporalUpscaleQuality (stereo_seq::debugCvarsTakeSsr):
// r_SSR is then not held (taa_ssr.hpp), so the status file never says its hold followed the game's
// Reflections setting. Each of the two the list holds is logged as taaLeftToDebugCvars does.
bool taaSsrLeftToDebugCvars();

} // namespace evr::vkcore
