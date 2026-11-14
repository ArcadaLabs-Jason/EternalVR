#pragma once

// Internal to CPU timing (cpu_timing.hpp), shared by cpu_timing.cpp (the stage timers, the per-tick split,
// the 10 s summary and the submit census it logs) and cpu_timing_hooks.cpp (the Vulkan call hooks).

#include <vulkan/vulkan.h>

#include <cstdint>

namespace evr::vkcore::cpu_timing::detail {

// Counts one vkQueueSubmit on the game's device in the submit census (gpu_timing/submit_census.hpp), from any
// thread; the reporter logs and clears it every window.
void recordSubmit(VkQueue queue, std::uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);

} // namespace evr::vkcore::cpu_timing::detail
