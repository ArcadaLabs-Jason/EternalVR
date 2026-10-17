#pragma once

// Fixed foveated rendering and its experiments through Khronos VK_KHR_fragment_shading_rate (AMD RDNA 2+,
// Intel Arc and NVIDIA when VK_NV_shading_rate_image is not used; docs/research/08-foveated-rendering.md).
// Shares the settings, eye shape latch, rate pattern generation, pass-eye tracking and presenter marks with
// vrs_nv (vrs_nv_impl.hpp).
//
// In Uniform mode (ETERNALVR_VRS_TEST=2x2 or 4x4) only pipelineFragmentShadingRate is needed, set per render
// pass via vkCmdSetFragmentShadingRateKHR. In spatial modes (ETERNALVR_FOVEATION, eyetest, fovea) single-subpass
// render passes are promoted to vkCreateRenderPass2 with a VK_FORMAT_R8_UINT shading rate attachment, and
// vkCmdSetFragmentShadingRateKHR switches combinerOp[1] between REPLACE (foveated eye pass) and KEEP (full rate
// pass: GUI target, shadow map, mono or unknown frame).

#include "vkcore/dispatch.hpp"

#include <vulkan/vulkan.h>

#include <vector>

namespace evr::vkcore::vrs_khr {

// At device creation (game device only, when vrs_nv::planDevice returned nullptr): adds
// VK_KHR_fragment_shading_rate (and VK_KHR_create_renderpass2 on Vulkan 1.1 drivers if needed) to `extensions`
// and returns the feature struct to chain (its pNext set by the caller), or nullptr when not wanted or not
// supported (logged).
VkPhysicalDeviceFragmentShadingRateFeaturesKHR*
planDevice(InstanceData& inst, VkPhysicalDevice physicalDevice, std::vector<const char*>& extensions);

// After the device is created with the plan. Registers the device in the shared VRS table so noteEye,
// noteSwapchain, passesFollowFrames and recordMarks work identically to vrs_nv.
void onDeviceCreated(DeviceData& data, bool enabled);

// vkCreateGraphicsPipelines, vkCreateRenderPass, vkDestroyRenderPass, vkCreateFramebuffer,
// vkDestroyFramebuffer, vkCmdBeginRenderPass and the command buffer / vrs_gui hooks while active on a KHR
// device; nullptr otherwise.
PFN_vkVoidFunction findHook(const char* name);

} // namespace evr::vkcore::vrs_khr

