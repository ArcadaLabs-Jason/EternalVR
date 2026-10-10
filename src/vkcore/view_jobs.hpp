#pragma once

// Parallel Eye Rendering's wrappers of whole engine jobs (Steam build 25216728), installed with the redirects
// (view_redirects.cpp):
// - frame work the per-view chain runs for each view, on state that exists once (skinning, the blended BLAS
//   and TLAS jobs, the GPU particle simulation, the geometry-cache upload, the pending-work drain): view 1's
//   call returns at once (docs/rig-findings/pe-frame-jobs.md);
// - jobs that use the renderer's scratch: the two views' calls take turns.

#include <cstddef>

namespace evr::vkcore {

// `mvpContext1` holds view 1's MVP-culling command context. False when a hook fails (logged).
bool installViewJobs(const std::byte* base, void* const* mvpContext1);

// Logs how many of view 1's calls returned at once.
void viewJobsLogCounts();

} // namespace evr::vkcore
