#pragma once

// Parallel Eye Rendering: async compute off, held by the layer. The engine keeps one set of async compute
// command contexts for the device (Post Processes 0x1C927D0, TSSAA 0x1C939A0, GPU particles, GPU culling,
// water, deferred passes: docs/rig-findings/view-shared-state.md section 2), so with both views' work at once
// both record into them: rig run pa1 crashed in the driver, and in the headset (launched from a .cmd, without
// +r_enableAsyncCompute 0) the game stopped presenting a few frames into the first world. A command line is
// not something a launch can be relied on to carry: the device setup (0x1CC34D0) reads r_enableAsyncCompute
// once, at 0x1CC3998 (ecx), and the hook makes that read 0 whatever the command line and the player's config
// say. The cvar itself is not written (the player's config keeps its value).
//
// - ETERNALVR_TEST_PE_ASYNC=keep (test only): the game's value stays, to reproduce the failure.
// - Safety net: if the game's device still has a compute-only queue (async compute on), the dispatcher
//   renders view 0 alone (one log line) instead of both views at once.

#include <vulkan/vulkan.h>

#include <cstddef>
#include <vector>

namespace evr::vkcore {

// A queue family the engine takes for async compute: compute without graphics.
inline bool computeOnlyFamily(VkQueueFlags flags) {
    return (flags & VK_QUEUE_COMPUTE_BIT) != 0 && (flags & VK_QUEUE_GRAPHICS_BIT) == 0;
}

// The device setup's read is the expected code (checked before anything is changed).
bool checkAsyncComputeRead(const std::byte* base);

// The hook on the device setup's read (after checkAsyncComputeRead); false when it cannot be installed.
bool installAsyncComputeOff(const std::byte* base);

// From the game's vkCreateDevice: notes whether the game asked for a compute-only queue.
void viewAsyncOnDevice(const VkDeviceCreateInfo& info, const std::vector<VkQueueFlags>& familyFlags);

// Parallel Eye Rendering is on and the game's device has an async compute queue all the same (without the
// test switch): the dispatcher then renders view 0 alone.
bool viewAsyncComputeOn();

} // namespace evr::vkcore
