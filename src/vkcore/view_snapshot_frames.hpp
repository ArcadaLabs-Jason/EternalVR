#pragma once

// Parallel Eye Rendering's eye snapshots (view_snapshot.hpp), the part view_snapshot_frames.cpp keeps: which
// swapchain image each command buffer being recorded moved to PRESENT_SRC since its begin, from the UI
// layer's command buffer hooks (view_snapshot::noteBegin and noteBarrier).

#include <vulkan/vulkan.h>

namespace evr::vkcore::view_snapshot {

// The swapchain image `cb` moved to PRESENT_SRC since its begin, or null; `released`: that barrier hands it
// to another queue family.
VkImage frameOf(VkCommandBuffer cb, bool* released = nullptr);

} // namespace evr::vkcore::view_snapshot
