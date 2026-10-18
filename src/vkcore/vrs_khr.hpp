#pragma once

// Foveated rendering and its experiments through Khronos VK_KHR_fragment_shading_rate, for GPUs without
// VK_NV_shading_rate_image (AMD RDNA 2 and later, Intel Arc; T-137, docs/research/08-foveated-rendering.md).
// It shares vrs_nv's settings, eye shape latch, rate patterns, pass eyes, GUI target check and presenter
// marks (vrs_nv_impl.hpp); only how the rate reaches the GPU differs.
//
// The game's single-subpass graphics render passes are made with vkCreateRenderPass2 and one more attachment,
// an R8_UINT shading rate attachment (vrs_khr_plan.hpp). Their framebuffers get a neutral, all full-rate rate
// image there, and their pipelines a fixed rate state: 1x1, the attachment's rate replacing it. A begin of an
// eye pass (as vrs_nv decides which passes those are) takes a variant of the game's framebuffer with that
// eye's rate image instead; every other begin keeps the neutral one, so full rate. No rate is set by command,
// so passes begun in any way and secondary command buffers have a defined rate.
//
// ETERNALVR_TEST_VRS_KHR=1 (a rig switch, not a player option) leaves VK_NV_shading_rate_image out, so this
// path is tried on an NVIDIA GPU too.

#include "vkcore/dispatch.hpp"

#include <vulkan/vulkan.h>

#include <vector>

namespace evr::vkcore::vrs_khr {

// At device creation (game device only), when vrs_nv::planDevice returned nullptr: adds
// VK_KHR_fragment_shading_rate (and VK_KHR_create_renderpass2 below Vulkan 1.2) to `extensions` unless
// listed already, and returns the attachmentFragmentShadingRate feature to chain (its pNext set by the
// caller); nullptr (logged) when foveated rendering is not wanted, the device lacks it, the device is below
// Vulkan 1.1, or the game's own create info uses fragment shading rates or imageless framebuffers.
VkPhysicalDeviceFragmentShadingRateFeaturesKHR* planDevice(InstanceData& inst,
                                                           VkPhysicalDevice physicalDevice,
                                                           const VkDeviceCreateInfo& info,
                                                           std::vector<const char*>& extensions);

// After the device is created with the plan's feature, from vrs_nv::onDeviceCreated (in place of its own
// set-up). The path is on for this device only once everything it calls was found.
void onDeviceCreated(DeviceData& data);

// vkCreateRenderPass, vkDestroyRenderPass, vkCreateFramebuffer, vkDestroyFramebuffer and
// vkCmdBeginRenderPass2[KHR] once some device has the path on; nullptr otherwise. They act only on such a
// device and call the next function on any other. vrs_nv::findHook asks this first.
PFN_vkVoidFunction findHook(const char* name);

} // namespace evr::vkcore::vrs_khr
