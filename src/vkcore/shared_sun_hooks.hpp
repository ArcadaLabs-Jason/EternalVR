#pragma once

// Eye R on eye L's sun cascades under Route S (stereo_seq/shared_sun.hpp,
// docs/rig-findings/perf-sun-cascades.md; Steam build 25216728). Off by default: ETERNALVR_SHARED_SUN=1 makes
// eye R keep the sun cascades eye L rendered instead of rendering them again, =count only counts,
// =ceiling is a rig bound (eye R renders no sun cascade at all).
//
// Two hooks in the shadow setup's record function (0x1CFE2B0): the stale check of a slice with a cache entry
// (`cmp edx, eax`, RVA 0x1CFF33E), where eye R keeps the entry instead of releasing it, and the listing
// (`test bl, 6`, RVA 0x1CFF357), which every slice the view renders passes. Route S only (installed from
// installSeqEyeFixes, never with alternate eyes or Parallel Eye Rendering, whose view_shadow_cache.cpp
// patches the same stretch, 0x1CFF2B8..0x1CFF357): whichever claims the check first keeps it for the process.
// Located by signature and cross-checked; anything missing leaves the game untouched and logs why. The
// multiplayer guard is asked at install and at every call.

namespace evr::vkcore {

// Locates and installs the hooks once per process when ETERNALVR_SHARED_SUN asks for them; later calls return
// the first result.
bool installSharedSunHooks();

// The shadow setup's stale check belongs to one feature per process: true when `owner` has it now (the first
// claim wins; the same owner may claim again).
enum class ShadowCheckOwner : int { SharedSun = 1, ParallelEye = 2 };
bool claimShadowCheck(ShadowCheckOwner owner);

} // namespace evr::vkcore
