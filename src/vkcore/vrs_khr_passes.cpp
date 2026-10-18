// Foveated rendering through VK_KHR_fragment_shading_rate (vrs_khr.hpp): the game's render passes get the
// shading rate attachment, and the pipelines made for them the rate state that takes the attachment's rate.

#include "vkcore/log.hpp"
#include "vkcore/vrs_khr_impl.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace evr::vkcore::vrs_khr {

namespace {

constexpr std::uint32_t kLoggedKept = 10;

} // namespace

bool isPromoted(KhrDevice& k, VkRenderPass renderPass) {
    std::shared_lock lock(k.mutex);
    return k.promoted.count(handleKey(renderPass)) != 0;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateRenderPass(VkDevice device,
                                                const VkRenderPassCreateInfo* pCreateInfo,
                                                const VkAllocationCallbacks* pAllocator,
                                                VkRenderPass* pRenderPass) {
    VrsDevice* d = vrs_nv::deviceOf(vrs_nv::keyOf(device));
    if (!d->khr) {
        return d->createRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
    }
    KhrDevice& k = *d->khrState;
    if (!promotes(*pCreateInfo)) {
        if (k.passesKept.fetch_add(1, std::memory_order_relaxed) < kLoggedKept) {
            EVR_LOG(
                "vrs: render pass kept as the game made it (full rate): %u subpass(es), %u attachment(s), "
                "first bind point %d, pNext type %d",
                pCreateInfo->subpassCount, pCreateInfo->attachmentCount,
                pCreateInfo->subpassCount > 0 ? static_cast<int>(pCreateInfo->pSubpasses[0].pipelineBindPoint)
                                              : -1,
                pCreateInfo->pNext
                    ? static_cast<int>(static_cast<const VkBaseInStructure*>(pCreateInfo->pNext)->sType)
                    : 0);
        }
        return d->createRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
    }
    const PromotedPass promoted(*pCreateInfo, d->texel);
    const VkResult result = d->createRenderPass2(device, &promoted.info(), pAllocator, pRenderPass);
    if (result != VK_SUCCESS) {
        // Made as the game asked instead: a pass without the attachment, whose framebuffers and pipelines
        // are then left as they are (full rate).
        k.passesKept.fetch_add(1, std::memory_order_relaxed);
        EVR_LOG(
            "vrs: the render pass with the shading rate attachment could not be made (%d, %u attachment(s)); "
            "made as the game asked, at full rate",
            static_cast<int>(result), pCreateInfo->attachmentCount);
        return d->createRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
    }
    {
        std::unique_lock lock(k.mutex);
        k.promoted.insert(handleKey(*pRenderPass));
    }
    if (k.passesPromoted.fetch_add(1, std::memory_order_relaxed) == 0) {
        EVR_LOG("vrs: first render pass made with the shading rate attachment (vkCreateRenderPass to "
                "VkRenderPassCreateInfo2, %u attachment(s) + 1, texel %ux%u)",
                pCreateInfo->attachmentCount, d->texel.width, d->texel.height);
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyRenderPass(VkDevice device,
                                             VkRenderPass renderPass,
                                             const VkAllocationCallbacks* pAllocator) {
    VrsDevice* d = vrs_nv::deviceOf(vrs_nv::keyOf(device));
    if (d->khr && renderPass != VK_NULL_HANDLE) {
        std::unique_lock lock(d->khrState->mutex);
        d->khrState->promoted.erase(handleKey(renderPass));
    }
    d->destroyRenderPass(device, renderPass, pAllocator);
}

VkResult createGraphicsPipelines(VrsDevice& d,
                                 VkDevice device,
                                 VkPipelineCache cache,
                                 std::uint32_t count,
                                 const VkGraphicsPipelineCreateInfo* pInfos,
                                 const VkAllocationCallbacks* pAllocator,
                                 VkPipeline* pPipelines) {
    KhrDevice& k = *d.khrState;
    // Pipelines of a promoted pass: 1x1, and the attachment's rate replaces it (the neutral image's 1x1 or
    // the eye's rate image). Other pipelines, of a pass without the attachment, are left as they are.
    std::vector<VkGraphicsPipelineCreateInfo> infos(pInfos, pInfos + count);
    std::vector<VkPipelineFragmentShadingRateStateCreateInfoKHR> rates(count);
    std::uint64_t changed = 0;
    std::uint64_t other = 0;
    std::uint64_t noPass = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (infos[i].renderPass == VK_NULL_HANDLE) {
            ++noPass;
            continue;
        }
        if (!isPromoted(k, infos[i].renderPass)) {
            ++other;
            continue;
        }
        rates[i] = {VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR};
        rates[i].pNext = infos[i].pNext;
        rates[i].fragmentSize = {1, 1};
        rates[i].combinerOps[0] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
        rates[i].combinerOps[1] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR;
        infos[i].pNext = &rates[i];
        ++changed;
    }
    k.pipelinesOther.fetch_add(other, std::memory_order_relaxed);
    k.pipelinesNoPass.fetch_add(noPass, std::memory_order_relaxed);
    if (k.pipelinesRate.fetch_add(changed, std::memory_order_relaxed) == 0 && changed > 0) {
        EVR_LOG("vrs: graphics pipelines of the promoted render passes get the rate state (1x1, the shading "
                "rate attachment's rate replacing it)");
    }
    return d.createGraphicsPipelines(device, cache, count, infos.data(), pAllocator, pPipelines);
}

} // namespace evr::vkcore::vrs_khr
