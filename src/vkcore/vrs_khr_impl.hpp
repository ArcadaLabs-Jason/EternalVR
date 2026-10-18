#pragma once

// Foveated rendering through VK_KHR_fragment_shading_rate's parts (vrs_khr.hpp): the device's state, the
// render passes and pipelines (vrs_khr_passes.cpp), the framebuffers and render pass begins
// (vrs_khr_begin.cpp) and the neutral rate images (vrs_khr_neutral.cpp).

#include "vkcore/vrs_khr_plan.hpp"
#include "vkcore/vrs_nv_impl.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace evr::vkcore::vrs_khr {

using vrs_nv::VrsDevice;

template <typename Handle>
std::uint64_t handleKey(Handle handle) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
}

// A full-rate (all zero) rate image for framebuffers of one size in texels: every promoted pass's framebuffer
// has one, so the pass is valid whatever its begin gets. Made and cleared like an eye's rate image: in each
// command buffer that begins a pass with it until one that did has had kReadySeconds to run.
struct NeutralImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkCommandBuffer clearedBy = VK_NULL_HANDLE;
    double firstClearedAt = 0.0; // 0: not cleared yet
};

// A framebuffer of a promoted pass: the game's attachments with the neutral rate image appended, and its
// variants with an eye's rate image instead, made at the first begin that wants each.
struct KhrFramebuffer {
    VkExtent2D extent{};
    std::uint32_t layers = 1;
    std::vector<VkImageView> attachments;                      // the game's
    std::uint64_t neutralKey = 0;                              // the neutral image's in KhrDevice::neutral
    std::unordered_map<std::uint64_t, VkFramebuffer> variants; // by the eye rate image's view
};

struct KhrDevice {
    VkExtent2D texel{16, 16}; // VrsDevice::texel, the attachment's texel (the device's largest)
    PFN_vkCmdClearColorImage cmdClearColorImage = nullptr;

    std::shared_mutex mutex; // the passes and framebuffers
    std::unordered_set<std::uint64_t> promoted;
    std::unordered_map<std::uint64_t, KhrFramebuffer> framebuffers;

    std::mutex neutralMutex;
    std::unordered_map<std::uint64_t, NeutralImage> neutral; // by size in texels (vrs_nv::packed)

    // What the rig's logs show (vrs_khr_begin.cpp's summary every 200,000 begins).
    std::array<std::atomic<std::uint64_t>, kBeginRates> rates{}; // by BeginRate
    std::atomic<std::uint64_t> notReady{0};    // Eye, but the eye's rate image was not ready
    std::atomic<std::uint64_t> notPromoted{0}; // begins of passes that were not promoted
    std::atomic<std::uint64_t> beginsV2{0};    // vkCmdBeginRenderPass2 (the rest are vkCmdBeginRenderPass)
    std::atomic<std::uint64_t> passesPromoted{0};
    std::atomic<std::uint64_t> passesKept{0};      // render passes the game made that were not promoted
    std::atomic<std::uint64_t> pipelinesRate{0};   // graphics pipelines given the rate state
    std::atomic<std::uint64_t> pipelinesOther{0};  // of a pass that was not promoted
    std::atomic<std::uint64_t> pipelinesNoPass{0}; // with no render pass (dynamic rendering)
};

// vrs_khr.cpp: what every device's VrsDevice needs for vrs_khr's hooks, which see every device once one has
// the path on (vrs_nv::onDeviceCreated and vrs_khr::onDeviceCreated call it).
void loadKhrPassThrough(VrsDevice& d);

// vrs_khr_passes.cpp: the hooks (each calls the next layer's function for a device without the path).
bool isPromoted(KhrDevice& k, VkRenderPass renderPass);
VKAPI_ATTR VkResult VKAPI_CALL CreateRenderPass(VkDevice device,
                                                const VkRenderPassCreateInfo* pCreateInfo,
                                                const VkAllocationCallbacks* pAllocator,
                                                VkRenderPass* pRenderPass);
VKAPI_ATTR void VKAPI_CALL DestroyRenderPass(VkDevice device,
                                             VkRenderPass renderPass,
                                             const VkAllocationCallbacks* pAllocator);
// vrs_nv's vkCreateGraphicsPipelines hook calls this for a device with the path on.
VkResult createGraphicsPipelines(VrsDevice& d,
                                 VkDevice device,
                                 VkPipelineCache cache,
                                 std::uint32_t count,
                                 const VkGraphicsPipelineCreateInfo* pInfos,
                                 const VkAllocationCallbacks* pAllocator,
                                 VkPipeline* pPipelines);

// vrs_khr_begin.cpp.
VKAPI_ATTR VkResult VKAPI_CALL CreateFramebuffer(VkDevice device,
                                                 const VkFramebufferCreateInfo* pCreateInfo,
                                                 const VkAllocationCallbacks* pAllocator,
                                                 VkFramebuffer* pFramebuffer);
VKAPI_ATTR void VKAPI_CALL DestroyFramebuffer(VkDevice device,
                                              VkFramebuffer framebuffer,
                                              const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                                               const VkRenderPassBeginInfo* pRenderPassBegin,
                                               const VkSubpassBeginInfo* pSubpassBeginInfo);
// vrs_nv's vkCmdBeginRenderPass hook calls this for a device with the path on.
void cmdBeginRenderPass(VrsDevice& d,
                        VkCommandBuffer commandBuffer,
                        const VkRenderPassBeginInfo& begin,
                        VkSubpassContents contents);

// vrs_khr_neutral.cpp: the neutral image's view for framebuffers of `texels` (made the first time; null when
// it cannot be made, nothing kept), and its clear recorded into `commandBuffer` where it is still needed
// (outside a render pass).
VkImageView neutralView(VrsDevice& d, VkExtent2D texels);
void prepareNeutral(VrsDevice& d, VkCommandBuffer commandBuffer, std::uint64_t key);

} // namespace evr::vkcore::vrs_khr
