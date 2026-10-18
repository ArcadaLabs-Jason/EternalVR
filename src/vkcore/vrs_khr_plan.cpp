// The device-free decisions of foveated rendering through VK_KHR_fragment_shading_rate (vrs_khr_plan.hpp).

#include "vkcore/vrs_khr_plan.hpp"

#include "features/foveation/rate_pattern.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace evr::vkcore::vrs_khr {

namespace {

constexpr std::uint8_t kKhr1x1 = 0x00;
constexpr std::uint8_t kKhr2x2 = 0x05;
constexpr std::uint8_t kKhr4x4 = 0x0A;

VkAttachmentReference2 toReference2(const VkAttachmentReference& r, VkImageAspectFlags aspects) {
    return {VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, r.attachment, r.layout,
            r.attachment == VK_ATTACHMENT_UNUSED ? 0u : aspects};
}

// The aspects of attachment `index` of `info` (none for VK_ATTACHMENT_UNUSED or an index out of range).
VkImageAspectFlags aspectsOf(const VkRenderPassCreateInfo& info, std::uint32_t index) {
    return index < info.attachmentCount ? attachmentAspects(info.pAttachments[index].format) : 0u;
}

} // namespace

std::vector<std::uint8_t> encodeRates(const std::vector<std::uint8_t>& pattern, bool allow4x4) {
    const std::uint8_t quarter = allow4x4 ? kKhr4x4 : kKhr2x2;
    std::vector<std::uint8_t> out(pattern.size());
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        out[i] = pattern[i] == foveation::kRateFull   ? kKhr1x1
                 : pattern[i] == foveation::kRateHalf ? kKhr2x2
                                                      : quarter;
    }
    return out;
}

VkExtent2D rateTexels(VkExtent2D extent, VkExtent2D texel) {
    return {(extent.width + texel.width - 1) / texel.width,
            (extent.height + texel.height - 1) / texel.height};
}

VkImageAspectFlags attachmentAspects(VkFormat format) {
    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case VK_FORMAT_S8_UINT:
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

bool promotes(const VkRenderPassCreateInfo& info) {
    return info.pNext == nullptr && info.subpassCount == 1 && info.pSubpasses != nullptr &&
           info.pSubpasses[0].pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS;
}

PromotedPass::PromotedPass(const VkRenderPassCreateInfo& info, VkExtent2D texel) {
    const std::uint32_t rate = info.attachmentCount;
    attachments_.resize(rate + 1);
    for (std::uint32_t i = 0; i < rate; ++i) {
        const VkAttachmentDescription& a = info.pAttachments[i];
        attachments_[i] = {VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                           nullptr,
                           a.flags,
                           a.format,
                           a.samples,
                           a.loadOp,
                           a.storeOp,
                           a.stencilLoadOp,
                           a.stencilStoreOp,
                           a.initialLayout,
                           a.finalLayout};
    }
    attachments_[rate] = {
        VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
        nullptr,
        0,
        VK_FORMAT_R8_UINT,
        VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_LOAD,
        VK_ATTACHMENT_STORE_OP_STORE, // DONT_CARE would leave its texels undefined after the pass
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR,
        VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR};

    const VkSubpassDescription& sub = info.pSubpasses[0];
    for (std::uint32_t i = 0; i < sub.inputAttachmentCount; ++i) {
        const VkAttachmentReference& r = sub.pInputAttachments[i];
        inputs_.push_back(toReference2(r, aspectsOf(info, r.attachment)));
    }
    for (std::uint32_t i = 0; i < sub.colorAttachmentCount; ++i) {
        colors_.push_back(toReference2(sub.pColorAttachments[i], VK_IMAGE_ASPECT_COLOR_BIT));
        if (sub.pResolveAttachments) {
            resolves_.push_back(toReference2(sub.pResolveAttachments[i], VK_IMAGE_ASPECT_COLOR_BIT));
        }
    }
    if (sub.pDepthStencilAttachment) {
        const VkAttachmentReference& r = *sub.pDepthStencilAttachment;
        depth_ = toReference2(r, aspectsOf(info, r.attachment));
    }
    rateRef_ = {VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, rate,
                VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR, 0};
    rateInfo_ = {VK_STRUCTURE_TYPE_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR, nullptr, &rateRef_, texel};
    subpass_ = {VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
                &rateInfo_,
                sub.flags,
                sub.pipelineBindPoint,
                0,
                static_cast<std::uint32_t>(inputs_.size()),
                inputs_.empty() ? nullptr : inputs_.data(),
                static_cast<std::uint32_t>(colors_.size()),
                colors_.empty() ? nullptr : colors_.data(),
                resolves_.empty() ? nullptr : resolves_.data(),
                sub.pDepthStencilAttachment ? &depth_ : nullptr,
                sub.preserveAttachmentCount,
                sub.pPreserveAttachments};
    for (std::uint32_t i = 0; i < info.dependencyCount; ++i) {
        const VkSubpassDependency& d = info.pDependencies[i];
        dependencies_.push_back({VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2, nullptr, d.srcSubpass, d.dstSubpass,
                                 d.srcStageMask, d.dstStageMask, d.srcAccessMask, d.dstAccessMask,
                                 d.dependencyFlags, 0});
    }
    info_ = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
             nullptr,
             info.flags,
             static_cast<std::uint32_t>(attachments_.size()),
             attachments_.data(),
             1,
             &subpass_,
             static_cast<std::uint32_t>(dependencies_.size()),
             dependencies_.empty() ? nullptr : dependencies_.data(),
             0,
             nullptr};
}

bool asksGuiTarget(const BeginShape& begin) {
    return (begin.eye == 0 || begin.eye == 1) && begin.renderArea.extent.width >= kMinFoveatedTarget &&
           begin.renderArea.extent.height >= kMinFoveatedTarget && begin.renderArea.offset.x == 0 &&
           begin.renderArea.offset.y == 0;
}

BeginRate rateFor(const BeginShape& begin) {
    const VkRect2D& area = begin.renderArea;
    if (begin.eye != 0 && begin.eye != 1) {
        return BeginRate::NoEye;
    }
    if (area.extent.width < kMinFoveatedTarget || area.extent.height < kMinFoveatedTarget) {
        return BeginRate::Small;
    }
    if (area.offset.x != 0 || area.offset.y != 0) {
        return BeginRate::Offset;
    }
    if (begin.guiTarget) {
        return BeginRate::GuiTarget;
    }
    if (!begin.uniform &&
        !foveation::isEyeSpaceTarget({area.extent.width, area.extent.height}, begin.eyeImage)) {
        return BeginRate::OtherTarget;
    }
    if (begin.secondary) {
        return BeginRate::Secondary;
    }
    if (begin.layers > 1) {
        return BeginRate::Layered;
    }
    if (area.extent.width != begin.framebuffer.width || area.extent.height != begin.framebuffer.height) {
        return BeginRate::PartialArea;
    }
    return BeginRate::Eye;
}

} // namespace evr::vkcore::vrs_khr
