// Fixed foveated rendering through VK_KHR_fragment_shading_rate (vrs_khr.hpp): device planning, render pass
// attachment promotion, framebuffer rate-view injection, pipeline state and per-pass combiner selection.

#include "vkcore/vrs_khr.hpp"

#include "features/foveation/eye_targets.hpp"
#include "features/foveation/rate_pattern.hpp"
#include "vkcore/dump_format.hpp"
#include "vkcore/log.hpp"
#include "vkcore/ui_vulkan.hpp"
#include "vkcore/vrs_gui.hpp"
#include "vkcore/vrs_nv.hpp"
#include "vkcore/vrs_nv_impl.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace evr::vkcore::vrs_khr {

namespace {

using vrs_nv::deviceOf, vrs_nv::DispatchKey, vrs_nv::keyOf, vrs_nv::kNoEye, vrs_nv::Mode,
    vrs_nv::Settings, vrs_nv::settings, vrs_nv::VrsDevice;

constexpr std::uint32_t kMinTarget = 256;

VkPhysicalDeviceFragmentShadingRateFeaturesKHR g_feature{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
std::atomic<bool> g_active{false};

struct FramebufferEntry {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkExtent2D extent{};
    std::uint32_t layers = 1;
    VkFramebufferCreateFlags flags = 0;
    std::vector<VkImageView> attachments; // original game attachments (excludes the appended VRS view)
    VkImageView boundView = VK_NULL_HANDLE;
    std::unordered_map<std::uint64_t, VkFramebuffer> variants; // by VkImageView handle
};

std::shared_mutex& g_passMutex = *new std::shared_mutex;
auto& g_promotedPasses = *new std::unordered_set<std::uint64_t>;
auto& g_framebuffers = *new std::unordered_map<std::uint64_t, FramebufferEntry>;

template <typename Handle>
std::uint64_t handleKey(Handle h) {
    return reinterpret_cast<std::uint64_t>(h);
}

bool isPromotedPass(VkRenderPass rp) {
    std::shared_lock lock(g_passMutex);
    return g_promotedPasses.find(handleKey(rp)) != g_promotedPasses.end();
}

VkAttachmentReference2 toRef2(const VkAttachmentReference& r, VkImageAspectFlags aspect) {
    return {VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, r.attachment, r.layout,
            r.attachment == VK_ATTACHMENT_UNUSED ? 0u : aspect};
}

VkImageAspectFlags depthStencilAspects(VkFormat format) {
    if (format == VK_FORMAT_S8_UINT) return VK_IMAGE_ASPECT_STENCIL_BIT;
    if (format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_X8_D24_UNORM_PACK32 || format == VK_FORMAT_D32_SFLOAT)
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateRenderPass(VkDevice device,
                                                const VkRenderPassCreateInfo* pCreateInfo,
                                                const VkAllocationCallbacks* pAllocator,
                                                VkRenderPass* pRenderPass) {
    VrsDevice* d = deviceOf(keyOf(device));
    if (!d || !d->on || !d->khrAttachment || !d->createRenderPass2 || !pCreateInfo) {
        return d ? d->createRenderPass(device, pCreateInfo, pAllocator, pRenderPass) : VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pCreateInfo->subpassCount != 1 || pCreateInfo->pNext != nullptr) {
        static std::atomic<std::uint32_t> s_skipCount{0};
        if (s_skipCount.fetch_add(1, std::memory_order_relaxed) < 10) {
            EVR_LOG("vrs: render pass not promoted: subpasses=%u, attachments=%u, pNext=%d",
                    pCreateInfo->subpassCount, pCreateInfo->attachmentCount, pCreateInfo->pNext != nullptr ? 1 : 0);
        }
        return d->createRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
    }
    const VkSubpassDescription& sub = pCreateInfo->pSubpasses[0];
    if (sub.pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS) {
        return d->createRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
    }

    const std::uint32_t vrsIdx = pCreateInfo->attachmentCount;
    std::vector<VkAttachmentDescription2> attachments(vrsIdx + 1);
    for (std::uint32_t i = 0; i < vrsIdx; ++i) {
        const VkAttachmentDescription& a = pCreateInfo->pAttachments[i];
        attachments[i] = {VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2, nullptr, a.flags, a.format, a.samples,
                          a.loadOp, a.storeOp, a.stencilLoadOp, a.stencilStoreOp, a.initialLayout,
                          a.finalLayout};
    }
    attachments[vrsIdx] = {VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2, nullptr, 0, VK_FORMAT_R8_UINT,
                           VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
                           VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                           VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR,
                           VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR};

    std::vector<VkAttachmentReference2> inputs(sub.inputAttachmentCount);
    for (std::uint32_t i = 0; i < sub.inputAttachmentCount; ++i)
        inputs[i] = toRef2(sub.pInputAttachments[i], VK_IMAGE_ASPECT_COLOR_BIT);
    std::vector<VkAttachmentReference2> colors(sub.colorAttachmentCount);
    std::vector<VkAttachmentReference2> resolves(sub.pResolveAttachments ? sub.colorAttachmentCount : 0);
    for (std::uint32_t i = 0; i < sub.colorAttachmentCount; ++i) {
        colors[i] = toRef2(sub.pColorAttachments[i], VK_IMAGE_ASPECT_COLOR_BIT);
        if (sub.pResolveAttachments) resolves[i] = toRef2(sub.pResolveAttachments[i], VK_IMAGE_ASPECT_COLOR_BIT);
    }
    VkAttachmentReference2 depth{};
    if (sub.pDepthStencilAttachment) {
        const std::uint32_t att = sub.pDepthStencilAttachment->attachment;
        const VkImageAspectFlags aspect =
            att < vrsIdx ? depthStencilAspects(pCreateInfo->pAttachments[att].format) : 0u;
        depth = toRef2(*sub.pDepthStencilAttachment, aspect);
    }

    VkAttachmentReference2 vrsRef{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, vrsIdx,
                                  VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR,
                                  VK_IMAGE_ASPECT_COLOR_BIT};
    VkFragmentShadingRateAttachmentInfoKHR fsrInfo{
        VK_STRUCTURE_TYPE_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR, nullptr, &vrsRef, d->texel};

    VkSubpassDescription2 sub2{VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2, &fsrInfo, sub.flags,
                               sub.pipelineBindPoint, 0, sub.inputAttachmentCount,
                               inputs.empty() ? nullptr : inputs.data(), sub.colorAttachmentCount,
                               colors.empty() ? nullptr : colors.data(),
                               resolves.empty() ? nullptr : resolves.data(),
                               sub.pDepthStencilAttachment ? &depth : nullptr, sub.preserveAttachmentCount,
                               sub.pPreserveAttachments};

    std::vector<VkSubpassDependency2> deps(pCreateInfo->dependencyCount);
    for (std::uint32_t i = 0; i < pCreateInfo->dependencyCount; ++i) {
        const auto& dep = pCreateInfo->pDependencies[i];
        deps[i] = {VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2, nullptr, dep.srcSubpass, dep.dstSubpass,
                   dep.srcStageMask, dep.dstStageMask, dep.srcAccessMask, dep.dstAccessMask, dep.dependencyFlags, 0};
    }

    VkRenderPassCreateInfo2 rp2{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2, nullptr, pCreateInfo->flags,
                                static_cast<std::uint32_t>(attachments.size()), attachments.data(), 1, &sub2,
                                static_cast<std::uint32_t>(deps.size()), deps.empty() ? nullptr : deps.data(),
                                0, nullptr};

    const VkResult res = d->createRenderPass2(device, &rp2, pAllocator, pRenderPass);
    if (res != VK_SUCCESS) {
        static std::atomic<std::uint32_t> s_failCount{0};
        if (s_failCount.fetch_add(1, std::memory_order_relaxed) < 10) {
            EVR_LOG("vrs: vkCreateRenderPass2 failed (%d): subpasses=%u, attachments=%u, pNext=%d; fallback",
                    static_cast<int>(res), pCreateInfo->subpassCount, pCreateInfo->attachmentCount,
                    pCreateInfo->pNext != nullptr ? 1 : 0);
        }
        return d->createRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
    }
    std::size_t total = 0;
    {
        std::unique_lock lock(g_passMutex);
        g_promotedPasses.insert(handleKey(*pRenderPass));
        total = g_promotedPasses.size();
    }
    if (total == 1) {
        EVR_LOG("vrs: first render pass promoted to vkCreateRenderPass2 with KHR shading rate attachment");
    }
    return res;
}

VKAPI_ATTR void VKAPI_CALL DestroyRenderPass(VkDevice device,
                                             VkRenderPass renderPass,
                                             const VkAllocationCallbacks* pAllocator) {
    VrsDevice* d = deviceOf(keyOf(device));
    if (renderPass != VK_NULL_HANDLE) {
        std::unique_lock lock(g_passMutex);
        g_promotedPasses.erase(handleKey(renderPass));
    }
    d->destroyRenderPass(device, renderPass, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateFramebuffer(VkDevice device,
                                                 const VkFramebufferCreateInfo* pCreateInfo,
                                                 const VkAllocationCallbacks* pAllocator,
                                                 VkFramebuffer* pFramebuffer) {
    VrsDevice* d = deviceOf(keyOf(device));
    if (!d || !d->on || !d->khrAttachment || !pCreateInfo || !isPromotedPass(pCreateInfo->renderPass)) {
        return d->createFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
    }
    const VkExtent2D extent{pCreateInfo->width, pCreateInfo->height};
    const VkExtent2D texels{(extent.width + d->texel.width - 1) / d->texel.width,
                            (extent.height + d->texel.height - 1) / d->texel.height};
    VkFramebufferCreateInfo copy = *pCreateInfo;

    // Handle imageless framebuffers if used.
    const auto* imageless = dump::findInChain<VkFramebufferAttachmentsCreateInfo>(
        pCreateInfo->pNext, VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO);
    VkFramebufferAttachmentsCreateInfo imagelessCopy{};
    std::vector<VkFramebufferAttachmentImageInfo> imageInfos;
    const VkFormat vrsFormat = VK_FORMAT_R8_UINT;
    if ((pCreateInfo->flags & VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT) && imageless) {
        imageInfos.assign(imageless->pAttachmentImageInfos,
                          imageless->pAttachmentImageInfos + imageless->attachmentImageInfoCount);
        VkFramebufferAttachmentImageInfo vrsImg{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO, nullptr, 0,
            VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            texels.width, texels.height, 1, 1, &vrsFormat};
        imageInfos.push_back(vrsImg);
        imagelessCopy = *imageless;
        imagelessCopy.attachmentImageInfoCount = static_cast<std::uint32_t>(imageInfos.size());
        imagelessCopy.pAttachmentImageInfos = imageInfos.data();
        copy.pNext = &imagelessCopy;
        copy.attachmentCount = imagelessCopy.attachmentImageInfoCount;
        const VkResult res = d->createFramebuffer(device, &copy, pAllocator, pFramebuffer);
        if (res == VK_SUCCESS) {
            std::unique_lock lock(g_passMutex);
            g_framebuffers[handleKey(*pFramebuffer)] = {pCreateInfo->renderPass, extent, pCreateInfo->layers,
                                                        pCreateInfo->flags, {}, VK_NULL_HANDLE, {}};
        }
        return res;
    }

    const VkImageView neutral = vrs_nv::neutralViewFor(*d, extent);
    if (neutral == VK_NULL_HANDLE) {
        return d->createFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
    }
    std::vector<VkImageView> views(pCreateInfo->pAttachments,
                                   pCreateInfo->pAttachments + pCreateInfo->attachmentCount);
    views.push_back(neutral);
    copy.attachmentCount = static_cast<std::uint32_t>(views.size());
    copy.pAttachments = views.data();
    const VkResult res = d->createFramebuffer(device, &copy, pAllocator, pFramebuffer);
    if (res == VK_SUCCESS) {
        std::vector<VkImageView> origViews(pCreateInfo->pAttachments,
                                           pCreateInfo->pAttachments + pCreateInfo->attachmentCount);
        std::unique_lock lock(g_passMutex);
        g_framebuffers[handleKey(*pFramebuffer)] = {pCreateInfo->renderPass, extent, pCreateInfo->layers,
                                                    pCreateInfo->flags, std::move(origViews), neutral, {}};
    }
    return res;
}

VKAPI_ATTR void VKAPI_CALL DestroyFramebuffer(VkDevice device,
                                              VkFramebuffer framebuffer,
                                              const VkAllocationCallbacks* pAllocator) {
    VrsDevice* d = deviceOf(keyOf(device));
    std::vector<VkFramebuffer> extra;
    if (framebuffer != VK_NULL_HANDLE) {
        std::unique_lock lock(g_passMutex);
        if (const auto it = g_framebuffers.find(handleKey(framebuffer)); it != g_framebuffers.end()) {
            for (const auto& [_, v] : it->second.variants) extra.push_back(v);
            g_framebuffers.erase(it);
        }
    }
    for (const VkFramebuffer v : extra) d->destroyFramebuffer(device, v, pAllocator);
    d->destroyFramebuffer(device, framebuffer, pAllocator);
}

VkFramebuffer variantFor(VrsDevice& d, VkFramebuffer base, VkImageView rateView) {
    {
        std::shared_lock lock(g_passMutex);
        const auto it = g_framebuffers.find(handleKey(base));
        if (it == g_framebuffers.end() || (it->second.flags & VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT) ||
            it->second.boundView == rateView) {
            return base;
        }
        const auto vit = it->second.variants.find(handleKey(rateView));
        if (vit != it->second.variants.end()) {
            return vit->second;
        }
    }
    std::unique_lock lock(g_passMutex);
    const auto it = g_framebuffers.find(handleKey(base));
    if (it == g_framebuffers.end()) {
        return base;
    }
    FramebufferEntry& entry = it->second;
    if (const auto vit = entry.variants.find(handleKey(rateView)); vit != entry.variants.end()) {
        return vit->second;
    }
    std::vector<VkImageView> views = entry.attachments;
    views.push_back(rateView);
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.flags = entry.flags;
    info.renderPass = entry.renderPass;
    info.attachmentCount = static_cast<std::uint32_t>(views.size());
    info.pAttachments = views.data();
    info.width = entry.extent.width;
    info.height = entry.extent.height;
    info.layers = entry.layers;
    VkFramebuffer created = VK_NULL_HANDLE;
    if (d.createFramebuffer(d.data->device, &info, nullptr, &created) != VK_SUCCESS) {
        return base;
    }
    entry.variants[handleKey(rateView)] = created;
    return created;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateGraphicsPipelines(VkDevice device,
                                                       VkPipelineCache cache,
                                                       std::uint32_t count,
                                                       const VkGraphicsPipelineCreateInfo* pInfos,
                                                       const VkAllocationCallbacks* pAllocator,
                                                       VkPipeline* pPipelines) {
    VrsDevice* d = deviceOf(keyOf(device));
    if (!d || !d->on) {
        return d->createGraphicsPipelines(device, cache, count, pInfos, pAllocator, pPipelines);
    }
    const Settings& s = settings();
    const std::uint32_t uniSide =
        s.mode == Mode::Uniform ? ((s.rate == foveation::kRateQuarter && d->khr4x4) ? 4u : 2u) : 1u;
    std::vector<VkGraphicsPipelineCreateInfo> infos(pInfos, pInfos + count);
    std::vector<VkPipelineFragmentShadingRateStateCreateInfoKHR> rates(count);
    std::vector<VkPipelineDynamicStateCreateInfo> dynamicStates(count);
    std::vector<std::vector<VkDynamicState>> dynamicStateLists(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        rates[i] = {VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR};
        rates[i].pNext = infos[i].pNext;
        rates[i].fragmentSize = {uniSide, uniSide};
        rates[i].combinerOps[0] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
        rates[i].combinerOps[1] = (s.mode != Mode::Uniform && d->khrAttachment)
                                      ? VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR
                                      : VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
        infos[i].pNext = &rates[i];

        const auto* origDyn = infos[i].pDynamicState;
        auto& list = dynamicStateLists[i];
        if (origDyn && origDyn->pDynamicStates && origDyn->dynamicStateCount > 0)
            list.assign(origDyn->pDynamicStates, origDyn->pDynamicStates + origDyn->dynamicStateCount);
        if (std::find(list.begin(), list.end(), VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR) == list.end())
            list.push_back(VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR);
        dynamicStates[i] = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                            origDyn ? origDyn->pNext : nullptr, origDyn ? origDyn->flags : 0u,
                            static_cast<std::uint32_t>(list.size()), list.data()};
        infos[i].pDynamicState = &dynamicStates[i];
    }
    if (d->pipelines.fetch_add(count) == 0 && count > 0) {
        EVR_LOG("vrs: graphics pipelines get VK_KHR_fragment_shading_rate state (%s)",
                d->khr4x4 ? "1x1, 2x2, 4x4" : "1x1, 2x2; 4x4 clamped to 2x2 on this GPU");
    }
    return d->createGraphicsPipelines(device, cache, count, infos.data(), pAllocator, pPipelines);
}

VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass(VkCommandBuffer commandBuffer,
                                              const VkRenderPassBeginInfo* pRenderPassBegin,
                                              VkSubpassContents contents) {
    VrsDevice* d = deviceOf(keyOf(commandBuffer));
    if (!d || !d->on || !pRenderPassBegin) {
        if (d && d->cmdBeginRenderPass) d->cmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
        return;
    }
    const VkExtent2D extent = pRenderPassBegin->renderArea.extent;
    const Settings& s = settings();
    const int eye = s.mode == Mode::Uniform ? 0 : vrs_nv::passEye(*d, commandBuffer);
    VkImageView view = VK_NULL_HANDLE;

    if (eye != kNoEye && extent.width >= kMinTarget && extent.height >= kMinTarget &&
        pRenderPassBegin->renderArea.offset.x == 0 && pRenderPassBegin->renderArea.offset.y == 0) {
        if (!vrs_gui::drawsGuiTarget(commandBuffer, *pRenderPassBegin)) {
            std::uint64_t size = d->eyeSize.load(std::memory_order_relaxed);
            if (size == 0) {
                size = d->swapchainSize.load(std::memory_order_relaxed);
            }
            const foveation::TargetSize eyeImage{static_cast<std::uint32_t>(size >> 32),
                                                 static_cast<std::uint32_t>(size)};
            if (s.mode != Mode::Uniform && d->khrAttachment &&
                foveation::isEyeSpaceTarget({extent.width, extent.height}, eyeImage)) {
                view = vrs_nv::viewFor(*d, commandBuffer, extent, eye);
            } else if (s.mode != Mode::Uniform && eyeImage.width != 0) {
                vrs_nv::noteOtherTarget(*d, extent, eyeImage);
            }
        } else {
            d->eyeSize.store(vrs_nv::packed(extent), std::memory_order_relaxed);
            if (d->guiPasses.fetch_add(1) == 0) {
                EVR_LOG("vrs: first render pass into the GUI target (image %p, %ux%u) kept at full rate; "
                        "the game's menus and HUD are not foveated",
                        reinterpret_cast<void*>(ui_vulkan::guiTarget()), extent.width, extent.height);
            }
        }
    }

    VkRenderPassBeginInfo beginCopy = *pRenderPassBegin;
    VkRenderPassAttachmentBeginInfo imagelessCopy{};
    std::vector<VkImageView> imagelessViews;
    bool swapped = false;
    const bool promoted = isPromotedPass(pRenderPassBegin->renderPass);

    if (d->khrAttachment && promoted) {
        vrs_nv::ensureNeutralLayout(*d, commandBuffer, extent);
        const auto* imageless = dump::findInChain<VkRenderPassAttachmentBeginInfo>(
            pRenderPassBegin->pNext, VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO);
        if (imageless) {
            const VkImageView chosen = view != VK_NULL_HANDLE ? view : vrs_nv::neutralViewFor(*d, extent);
            if (chosen != VK_NULL_HANDLE) {
                imagelessViews.assign(imageless->pAttachments,
                                      imageless->pAttachments + imageless->attachmentCount);
                imagelessViews.push_back(chosen);
                imagelessCopy = *imageless;
                imagelessCopy.attachmentCount = static_cast<std::uint32_t>(imagelessViews.size());
                imagelessCopy.pAttachments = imagelessViews.data();
                beginCopy.pNext = &imagelessCopy;
                swapped = (view != VK_NULL_HANDLE);
            }
        } else if (view != VK_NULL_HANDLE) {
            beginCopy.framebuffer = variantFor(*d, pRenderPassBegin->framebuffer, view);
            swapped = (beginCopy.framebuffer != pRenderPassBegin->framebuffer);
        }
    }

    const bool foveate = (s.mode == Mode::Uniform) || ((view != VK_NULL_HANDLE) && swapped);

    if (view != VK_NULL_HANDLE && !swapped) {
        static std::atomic<std::uint32_t> s_warnCount{0};
        if (s_warnCount.fetch_add(1, std::memory_order_relaxed) < 5) {
            bool fbKnown = false;
            {
                std::shared_lock lock(g_passMutex);
                fbKnown = g_framebuffers.find(handleKey(pRenderPassBegin->framebuffer)) != g_framebuffers.end();
            }
            EVR_LOG("vrs: eye pass %ux%u failed framebuffer swap (promoted=%d, fbKnown=%d)",
                    extent.width, extent.height, promoted ? 1 : 0, fbKnown ? 1 : 0);
        }
    }

    d->binds[static_cast<std::size_t>(eye)].fetch_add(1);
    if (foveate) {
        if (d->coarse[static_cast<std::size_t>(eye)].fetch_add(1, std::memory_order_relaxed) == 0) {
            EVR_LOG("vrs: first foveated render pass recorded for eye %d (%ux%u, KHR attachment active, "
                    "promoted=%d, swappedFb=%d)",
                    eye, extent.width, extent.height, promoted ? 1 : 0, swapped ? 1 : 0);
        }
    }
    const std::uint64_t full = foveate ? d->fullRate.load() : d->fullRate.fetch_add(1) + 1;
    if (d->passes.fetch_add(1) % 200000 == 199999) {
        EVR_LOG("vrs: render passes (KHR): %llu recorded for eye L, %llu for eye R, %llu mono, untagged or "
                "not known; %llu of them at full rate (%llu into the GUI target); %llu pipeline(s)",
                static_cast<unsigned long long>(d->binds[0].load()),
                static_cast<unsigned long long>(d->binds[1].load()),
                static_cast<unsigned long long>(d->binds[2].load()),
                static_cast<unsigned long long>(full),
                static_cast<unsigned long long>(d->guiPasses.load()),
                static_cast<unsigned long long>(d->pipelines.load()));
        if (s.mode != Mode::Uniform) {
            vrs_nv::logPassFrames(*d);
        }
    }
    if (d->cmdSetFragmentShadingRate) {
        const std::uint32_t uniSide =
            s.mode == Mode::Uniform ? ((s.rate == foveation::kRateQuarter && d->khr4x4) ? 4u : 2u) : 1u;
        const VkExtent2D fragSize{uniSide, uniSide};
        const VkFragmentShadingRateCombinerOpKHR combiners[2] = {
            VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR,
            (foveate && d->khrAttachment) ? VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR
                                          : VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR,
        };
        d->cmdSetFragmentShadingRate(commandBuffer, &fragSize, combiners);
        d->cmdBeginRenderPass(commandBuffer, &beginCopy, contents);
        d->cmdSetFragmentShadingRate(commandBuffer, &fragSize, combiners);
    } else {
        d->cmdBeginRenderPass(commandBuffer, &beginCopy, contents);
    }
}

} // namespace

VkPhysicalDeviceFragmentShadingRateFeaturesKHR*
planDevice(InstanceData& inst, VkPhysicalDevice physicalDevice, std::vector<const char*>& extensions) {
    if (!vrs_nv::wanted()) {
        return nullptr;
    }
    std::uint32_t count = 0;
    inst.vk.EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    inst.vk.EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, available.data());
    auto hasExt = [&](const char* name) {
        return std::any_of(available.begin(), available.end(), [name](const VkExtensionProperties& p) {
            return std::strcmp(p.extensionName, name) == 0;
        });
    };
    if (!hasExt(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME) || !inst.vk.GetPhysicalDeviceFeatures2) {
        EVR_LOG("vrs: neither %s nor %s is supported on this device; foveated rendering stays off",
                VK_NV_SHADING_RATE_IMAGE_EXTENSION_NAME, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
        return nullptr;
    }
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR query{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &query};
    inst.vk.GetPhysicalDeviceFeatures2(physicalDevice, &features);

    const bool needAttachment = settings().mode != Mode::Uniform;
    if (!query.pipelineFragmentShadingRate || (needAttachment && !query.attachmentFragmentShadingRate)) {
        EVR_LOG("vrs: %s lacks %s on this device; foveated rendering stays off",
                VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME,
                needAttachment ? "attachmentFragmentShadingRate" : "pipelineFragmentShadingRate");
        return nullptr;
    }
    if (needAttachment && hasExt(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME)) {
        if (std::find_if(extensions.begin(), extensions.end(), [](const char* e) {
                return std::strcmp(e, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME) == 0;
            }) == extensions.end()) {
            extensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
        }
    }
    extensions.push_back(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
    g_feature = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
    g_feature.pipelineFragmentShadingRate = VK_TRUE;
    g_feature.attachmentFragmentShadingRate = query.attachmentFragmentShadingRate ? VK_TRUE : VK_FALSE;
    g_active.store(true, std::memory_order_relaxed);
    EVR_LOG("  adding device extension %s (foveated rendering for AMD/Khronos)",
            VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
    return &g_feature;
}

void onDeviceCreated(DeviceData& data, bool enabled) {
    if (!vrs_nv::wanted() || !enabled) {
        return;
    }
    auto* d = new VrsDevice;
    d->data = &data;
    d->khr = true;
    const auto load = [&](const char* n) { return data.nextGetDeviceProcAddr(data.device, n); };
#define EVR_L(m, fn) d->m = reinterpret_cast<PFN_vk##fn>(load("vk" #fn))
    EVR_L(createGraphicsPipelines, CreateGraphicsPipelines);
    EVR_L(cmdBeginRenderPass, CmdBeginRenderPass);
    EVR_L(cmdSetFragmentShadingRate, CmdSetFragmentShadingRateKHR);
    EVR_L(createRenderPass, CreateRenderPass);
    EVR_L(createRenderPass2, CreateRenderPass2);
    if (!d->createRenderPass2) d->createRenderPass2 = reinterpret_cast<PFN_vkCreateRenderPass2>(load("vkCreateRenderPass2KHR"));
    EVR_L(destroyRenderPass, DestroyRenderPass);
    EVR_L(createFramebuffer, CreateFramebuffer);
    EVR_L(destroyFramebuffer, DestroyFramebuffer);
    EVR_L(createImageView, CreateImageView);
    EVR_L(destroyImageView, DestroyImageView);
    EVR_L(cmdCopyBufferToImage, CmdCopyBufferToImage);
#undef EVR_L
    vrs_nv::loadCommandBufferFunctions(*d);

    VkPhysicalDeviceFragmentShadingRatePropertiesKHR props{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &props};
    data.instance->vk.GetPhysicalDeviceProperties2(data.physicalDevice, &props2);

    d->texel = props.maxFragmentShadingRateAttachmentTexelSize.width > 0
                   ? props.maxFragmentShadingRateAttachmentTexelSize
                   : VkExtent2D{16, 16};
    d->khr4x4 = props.maxFragmentSize.width >= 4 && props.maxFragmentSize.height >= 4;
    d->khrAttachment = g_feature.attachmentFragmentShadingRate == VK_TRUE && d->createRenderPass &&
                       d->createRenderPass2 && d->destroyRenderPass && d->createFramebuffer &&
                       d->destroyFramebuffer && d->createImageView && d->cmdCopyBufferToImage;
    const Settings& s = settings();
    d->on = d->cmdSetFragmentShadingRate && d->createGraphicsPipelines && d->cmdBeginRenderPass &&
            (s.mode == Mode::Uniform || d->khrAttachment);
    d->followsFrames = d->on && s.mode != Mode::Uniform;
    d->usesGuesses = d->followsFrames && s.guesses;
    if (d->followsFrames) {
        vrs_nv::noteFollowsFrames();
    }
    g_active.store(d->on, std::memory_order_relaxed);
    EVR_LOG("vrs: KHR %s: rate texel %ux%u, max fragment %ux%u (4x4 %s); %s", d->on ? "on" : "off",
            d->texel.width, d->texel.height, props.maxFragmentSize.width, props.maxFragmentSize.height,
            d->khr4x4 ? "supported" : "clamped to 2x2",
            s.mode == Mode::Uniform ? "uniform pipeline shading rate"
            : s.mode == Mode::EyeTest ? "eye test via KHR attachment"
                                      : "fixed foveation around head-forward via KHR attachment");
    vrs_gui::onDeviceCreated(data, d->on);
    if (const PFN_vkVoidFunction f = vrs_gui::findHook("vkCreateFramebuffer"))
        d->createFramebuffer = reinterpret_cast<PFN_vkCreateFramebuffer>(f);
    if (const PFN_vkVoidFunction f = vrs_gui::findHook("vkDestroyFramebuffer"))
        d->destroyFramebuffer = reinterpret_cast<PFN_vkDestroyFramebuffer>(f);
    vrs_nv::registerDevice(keyOf(data.device), d);
}

PFN_vkVoidFunction findHook(const char* name) {
    if (!vrs_nv::wanted() || !g_active.load(std::memory_order_relaxed)) {
        return nullptr;
    }
#define EVR_VRS_KHR_HOOK(fn)                                                                                 \
    if (std::strcmp(name, "vk" #fn) == 0) {                                                                  \
        return reinterpret_cast<PFN_vkVoidFunction>(&fn);                                                    \
    }
    EVR_VRS_KHR_HOOK(CreateGraphicsPipelines)
    EVR_VRS_KHR_HOOK(CmdBeginRenderPass)
    EVR_VRS_KHR_HOOK(CreateRenderPass)
    EVR_VRS_KHR_HOOK(DestroyRenderPass)
    EVR_VRS_KHR_HOOK(CreateFramebuffer)
    EVR_VRS_KHR_HOOK(DestroyFramebuffer)
#undef EVR_VRS_KHR_HOOK
    if (const PFN_vkVoidFunction hook = vrs_nv::findCommandBufferHook(name)) return hook;
    return vrs_gui::findHook(name);
}

} // namespace evr::vkcore::vrs_khr

