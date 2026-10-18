#pragma once

// The decisions of foveated rendering through VK_KHR_fragment_shading_rate (vrs_khr.hpp) that need no
// device, tested on their own: the rate image's texel values, which of the game's render passes get the
// shading rate attachment and how, and which render pass begins get the eye's rate image.

#include "features/foveation/eye_targets.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace evr::vkcore::vrs_khr {

// Render areas smaller than this (in either direction) keep full rate: shadow cascades, small effects (the
// same as vrs_nv's).
inline constexpr std::uint32_t kMinFoveatedTarget = 256;

// A render pass's eye as vrs_nv gives it: 0 (L), 1 (R), anything else none (mono, untagged, not known).
inline constexpr int kNoEye = 2;

// The rate image's texels for `pattern` (foveation::kRateFull, kRateHalf, kRateQuarter per texel, from
// rate_pattern.hpp): VK_KHR_fragment_shading_rate's value (log2(width) << 2) | log2(height), so 1x1 = 0,
// 2x2 = 5, 4x4 = 10. Quarter rate becomes 2x2 when the device's largest fragment is smaller than 4x4.
[[nodiscard]] std::vector<std::uint8_t> encodeRates(const std::vector<std::uint8_t>& pattern, bool allow4x4);

// The rate image's size in texels for a framebuffer of `extent` (ceil(extent / texel) each way).
[[nodiscard]] VkExtent2D rateTexels(VkExtent2D extent, VkExtent2D texel);

// The aspects an attachment of `format` is read through as an input attachment: depth and stencil for the
// depth/stencil formats (as many of them as the format has), colour otherwise.
[[nodiscard]] VkImageAspectFlags attachmentAspects(VkFormat format);

// Whether the game's render pass gets the shading rate attachment: one graphics subpass and nothing chained
// (multiview, input attachment aspects and the other VkRenderPassCreateInfo extensions are not carried over
// to VkRenderPassCreateInfo2, so such a pass stays as the game made it, at full rate).
[[nodiscard]] bool promotes(const VkRenderPassCreateInfo& info);

// The game's render pass as a VkRenderPassCreateInfo2 with one more attachment, the R8_UINT shading rate
// attachment (loaded, in FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR throughout), referenced by the subpass
// with `texel` pixels per rate texel. Only for a pass `promotes` accepts. Holds the arrays info() points at.
class PromotedPass {
public:
    PromotedPass(const VkRenderPassCreateInfo& info, VkExtent2D texel);
    PromotedPass(const PromotedPass&) = delete;
    PromotedPass& operator=(const PromotedPass&) = delete;

    [[nodiscard]] const VkRenderPassCreateInfo2& info() const { return info_; }
    // The shading rate attachment's index: the game's attachment count.
    [[nodiscard]] std::uint32_t rateAttachment() const { return rateRef_.attachment; }

private:
    std::vector<VkAttachmentDescription2> attachments_;
    std::vector<VkAttachmentReference2> inputs_;
    std::vector<VkAttachmentReference2> colors_;
    std::vector<VkAttachmentReference2> resolves_;
    VkAttachmentReference2 depth_{};
    VkAttachmentReference2 rateRef_{};
    VkFragmentShadingRateAttachmentInfoKHR rateInfo_{};
    VkSubpassDescription2 subpass_{};
    std::vector<VkSubpassDependency2> dependencies_;
    VkRenderPassCreateInfo2 info_{};
};

// Why a render pass begin of a promoted pass gets the eye's rate image (Eye) or keeps full rate through the
// neutral one (every other value). The order is the order of the checks.
enum class BeginRate {
    Eye,
    NoEye,       // a mono, untagged or not known frame
    Small,       // the render area is under kMinFoveatedTarget either way
    Offset,      // the render area does not start at the origin
    GuiTarget,   // it draws into the game's GUI target (menus and HUD)
    OtherTarget, // not the eye image or a scaled copy of it (shadow maps, look-up tables)
    Secondary,   // its contents are secondary command buffers, which may name the game's framebuffer
    Layered,     // a layered framebuffer: the eye's pattern is for one view
    PartialArea, // the render area is not the whole framebuffer: the rate image would not cover it
};
inline constexpr int kBeginRates = 9;

struct BeginShape {
    bool uniform = false;  // ETERNALVR_VRS_TEST=2x2 or 4x4: every eligible pass, as eye 0, in any target
    int eye = kNoEye;      // the pass's eye (vrs_nv::passEye), 0 in uniform mode
    VkRect2D renderArea{}; // VkRenderPassBeginInfo::renderArea
    VkExtent2D framebuffer{};
    std::uint32_t layers = 1;
    bool secondary = false; // VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS
    bool guiTarget = false; // vrs_gui::drawsGuiTarget, asked only for a pass past the first three checks
    foveation::TargetSize eyeImage;
};

// True when a begin past NoEye, Small and Offset, the only ones whose GUI target is asked.
[[nodiscard]] bool asksGuiTarget(const BeginShape& begin);
[[nodiscard]] BeginRate rateFor(const BeginShape& begin);

} // namespace evr::vkcore::vrs_khr
