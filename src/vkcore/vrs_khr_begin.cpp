// Foveated rendering through VK_KHR_fragment_shading_rate (vrs_khr.hpp): the framebuffers of the promoted
// render passes, and the begins that take the eye's rate image.

#include "features/foveation/eye_targets.hpp"
#include "vkcore/log.hpp"
#include "vkcore/ui_vulkan.hpp"
#include "vkcore/vrs_gui.hpp"
#include "vkcore/vrs_khr_impl.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

namespace evr::vkcore::vrs_khr {

namespace {

using vrs_nv::Mode;

// The game's framebuffer with `rateView` for its rate image, made with the begin's own render pass (the one
// the game made the framebuffer with may be gone; any pass it begins is compatible with it). `base` itself
// when that fails.
VkFramebuffer variantFor(VrsDevice& d, VkFramebuffer base, VkRenderPass renderPass, VkImageView rateView) {
    KhrDevice& k = *d.khrState;
    {
        std::shared_lock lock(k.mutex);
        const auto it = k.framebuffers.find(handleKey(base));
        if (it == k.framebuffers.end()) {
            return base;
        }
        if (const auto v = it->second.variants.find(handleKey(rateView)); v != it->second.variants.end()) {
            return v->second;
        }
    }
    std::unique_lock lock(k.mutex);
    const auto it = k.framebuffers.find(handleKey(base));
    if (it == k.framebuffers.end()) {
        return base;
    }
    KhrFramebuffer& entry = it->second;
    if (const auto v = entry.variants.find(handleKey(rateView)); v != entry.variants.end()) {
        return v->second;
    }
    std::vector<VkImageView> views = entry.attachments;
    views.push_back(rateView);
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = renderPass;
    info.attachmentCount = static_cast<std::uint32_t>(views.size());
    info.pAttachments = views.data();
    info.width = entry.extent.width;
    info.height = entry.extent.height;
    info.layers = entry.layers;
    VkFramebuffer created = VK_NULL_HANDLE;
    if (d.createFramebuffer(d.data->device, &info, nullptr, &created) != VK_SUCCESS) {
        return base;
    }
    entry.variants.emplace(handleKey(rateView), created);
    return created;
}

void logSummary(VrsDevice& d) {
    KhrDevice& k = *d.khrState;
    const auto n = [](const std::atomic<std::uint64_t>& v) {
        return static_cast<unsigned long long>(v.load(std::memory_order_relaxed));
    };
    const auto r = [&](BeginRate rate) {
        return n(k.rates[static_cast<std::size_t>(rate)]);
    };
    EVR_LOG(
        "vrs: render passes (KHR): %llu recorded for eye L, %llu for eye R, %llu mono, untagged or not "
        "known; %llu with the eye's rate image; at full rate: %llu not an eye's frame, %llu small, %llu "
        "offset, %llu into the GUI target, %llu other targets, %llu with secondary command buffers, %llu "
        "layered, %llu not the whole framebuffer, %llu rate image not ready, %llu of passes not promoted; "
        "%llu begun with vkCmdBeginRenderPass2",
        n(d.binds[0]), n(d.binds[1]), n(d.binds[2]), r(BeginRate::Eye) - n(k.notReady), r(BeginRate::NoEye),
        r(BeginRate::Small), r(BeginRate::Offset), r(BeginRate::GuiTarget), r(BeginRate::OtherTarget),
        r(BeginRate::Secondary), r(BeginRate::Layered), r(BeginRate::PartialArea), n(k.notReady),
        n(k.notPromoted), n(k.beginsV2));
    EVR_LOG("vrs: render passes made (KHR): %llu with the shading rate attachment, %llu without; graphics "
            "pipelines: %llu with the rate state, %llu of other passes, %llu without a render pass",
            n(k.passesPromoted), n(k.passesKept), n(k.pipelinesRate), n(k.pipelinesOther),
            n(k.pipelinesNoPass));
    if (vrs_nv::settings().mode != Mode::Uniform) {
        vrs_nv::logPassFrames(d);
    }
}

// What the begin gets: the game's framebuffer (neutral rate image, cleared where needed) or a variant with
// the eye's rate image. Counts it.
VkFramebuffer framebufferFor(VrsDevice& d,
                             VkCommandBuffer commandBuffer,
                             const VkRenderPassBeginInfo& begin,
                             bool secondary,
                             bool v2) {
    KhrDevice& k = *d.khrState;
    if (v2) {
        k.beginsV2.fetch_add(1, std::memory_order_relaxed);
    }
    if (!isPromoted(k, begin.renderPass)) {
        k.notPromoted.fetch_add(1, std::memory_order_relaxed);
        return begin.framebuffer;
    }
    KhrFramebuffer shape;
    {
        std::shared_lock lock(k.mutex);
        const auto it = k.framebuffers.find(handleKey(begin.framebuffer));
        if (it == k.framebuffers.end()) {
            return begin.framebuffer; // not made through the hook: cannot happen once the pass is promoted
        }
        shape.extent = it->second.extent;
        shape.layers = it->second.layers;
        shape.neutralKey = it->second.neutralKey;
    }
    const vrs_nv::Settings& s = vrs_nv::settings();
    BeginShape b;
    b.uniform = s.mode == Mode::Uniform;
    b.eye = b.uniform ? 0 : vrs_nv::passEye(d, commandBuffer);
    b.renderArea = begin.renderArea;
    b.framebuffer = shape.extent;
    b.layers = shape.layers;
    b.secondary = secondary;
    std::uint64_t size = d.eyeSize.load(std::memory_order_relaxed);
    if (size == 0) {
        size = d.swapchainSize.load(std::memory_order_relaxed);
    }
    b.eyeImage = {static_cast<std::uint32_t>(size >> 32), static_cast<std::uint32_t>(size)};
    b.guiTarget = asksGuiTarget(b) && vrs_gui::drawsGuiTarget(commandBuffer, begin);
    const BeginRate rate = rateFor(b);
    const VkExtent2D extent = begin.renderArea.extent;
    if (rate == BeginRate::GuiTarget) {
        d.eyeSize.store(vrs_nv::packed(extent), std::memory_order_relaxed);
        if (d.guiPasses.fetch_add(1) == 0) {
            EVR_LOG(
                "vrs: first render pass into the GUI target (image %p, %ux%u) kept at full rate; the game's "
                "menus and HUD are not foveated",
                reinterpret_cast<void*>(ui_vulkan::guiTarget()), extent.width, extent.height);
        }
    } else if (rate == BeginRate::OtherTarget && b.eyeImage.width != 0) {
        vrs_nv::noteOtherTarget(d, extent, b.eyeImage);
    }
    k.rates[static_cast<std::size_t>(rate)].fetch_add(1, std::memory_order_relaxed);
    const int eye = b.eye == 0 || b.eye == 1 ? b.eye : kNoEye;
    d.binds[static_cast<std::size_t>(eye)].fetch_add(1);
    VkFramebuffer framebuffer = begin.framebuffer;
    if (rate == BeginRate::Eye) {
        const VkImageView view = vrs_nv::viewFor(d, commandBuffer, extent, eye);
        framebuffer = view ? variantFor(d, begin.framebuffer, begin.renderPass, view) : begin.framebuffer;
        if (framebuffer == begin.framebuffer) {
            k.notReady.fetch_add(1, std::memory_order_relaxed);
        } else if (d.coarse[static_cast<std::size_t>(eye)].fetch_add(1, std::memory_order_relaxed) == 0) {
            EVR_LOG("vrs: first render pass with eye %d's rate image (%ux%u)", eye, extent.width,
                    extent.height);
        }
    }
    if (framebuffer == begin.framebuffer) {
        d.fullRate.fetch_add(1, std::memory_order_relaxed);
        prepareNeutral(d, commandBuffer, shape.neutralKey);
    }
    if (d.passes.fetch_add(1) % 200000 == 199999) {
        logSummary(d);
    }
    return framebuffer;
}

} // namespace

VKAPI_ATTR VkResult VKAPI_CALL CreateFramebuffer(VkDevice device,
                                                 const VkFramebufferCreateInfo* pCreateInfo,
                                                 const VkAllocationCallbacks* pAllocator,
                                                 VkFramebuffer* pFramebuffer) {
    VrsDevice* d = vrs_nv::deviceOf(vrs_nv::keyOf(device));
    if (!d->khr || !isPromoted(*d->khrState, pCreateInfo->renderPass)) {
        return d->createFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
    }
    // Not imageless: planDevice leaves the path off where the game enables imageless framebuffers.
    const VkExtent2D extent{pCreateInfo->width, pCreateInfo->height};
    const VkExtent2D texels = rateTexels(extent, d->texel);
    const VkImageView neutral = neutralView(*d, texels);
    if (neutral == VK_NULL_HANDLE) {
        // The pass has one more attachment than the game gives: without a rate image no framebuffer of it is
        // valid, so this create fails as the driver's would without memory.
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    std::vector<VkImageView> views(pCreateInfo->pAttachments,
                                   pCreateInfo->pAttachments + pCreateInfo->attachmentCount);
    views.push_back(neutral);
    VkFramebufferCreateInfo info = *pCreateInfo;
    info.attachmentCount = static_cast<std::uint32_t>(views.size());
    info.pAttachments = views.data();
    const VkResult result = d->createFramebuffer(device, &info, pAllocator, pFramebuffer);
    if (result == VK_SUCCESS) {
        views.pop_back();
        KhrFramebuffer entry;
        entry.extent = extent;
        entry.layers = pCreateInfo->layers;
        entry.attachments = std::move(views);
        entry.neutralKey = vrs_nv::packed(texels);
        std::unique_lock lock(d->khrState->mutex);
        d->khrState->framebuffers[handleKey(*pFramebuffer)] = std::move(entry);
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyFramebuffer(VkDevice device,
                                              VkFramebuffer framebuffer,
                                              const VkAllocationCallbacks* pAllocator) {
    VrsDevice* d = vrs_nv::deviceOf(vrs_nv::keyOf(device));
    std::vector<VkFramebuffer> variants;
    if (d->khr && framebuffer != VK_NULL_HANDLE) {
        std::unique_lock lock(d->khrState->mutex);
        if (const auto it = d->khrState->framebuffers.find(handleKey(framebuffer));
            it != d->khrState->framebuffers.end()) {
            for (const auto& [view, variant] : it->second.variants) {
                variants.push_back(variant);
            }
            d->khrState->framebuffers.erase(it);
        }
    }
    for (const VkFramebuffer variant : variants) {
        d->destroyFramebuffer(device, variant, nullptr);
    }
    d->destroyFramebuffer(device, framebuffer, pAllocator);
}

void cmdBeginRenderPass(VrsDevice& d,
                        VkCommandBuffer commandBuffer,
                        const VkRenderPassBeginInfo& begin,
                        VkSubpassContents contents) {
    VkRenderPassBeginInfo copy = begin;
    copy.framebuffer = framebufferFor(d, commandBuffer, begin,
                                      contents == VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS, false);
    d.cmdBeginRenderPass(commandBuffer, &copy, contents);
}

VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                                               const VkRenderPassBeginInfo* pRenderPassBegin,
                                               const VkSubpassBeginInfo* pSubpassBeginInfo) {
    VrsDevice* d = vrs_nv::deviceOf(vrs_nv::keyOf(commandBuffer));
    if (!d->khr) {
        d->cmdBeginRenderPass2(commandBuffer, pRenderPassBegin, pSubpassBeginInfo);
        return;
    }
    VkRenderPassBeginInfo copy = *pRenderPassBegin;
    copy.framebuffer =
        framebufferFor(*d, commandBuffer, *pRenderPassBegin,
                       pSubpassBeginInfo->contents == VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS, true);
    d->cmdBeginRenderPass2(commandBuffer, &copy, pSubpassBeginInfo);
}

} // namespace evr::vkcore::vrs_khr
