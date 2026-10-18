#include "vkcore/vrs_khr_plan.hpp"

#include "features/foveation/rate_pattern.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

namespace khr = evr::vkcore::vrs_khr;
using khr::BeginRate;
using khr::BeginShape;

namespace {

// Sizes from the rig's foveation runs (headset-test rel-0.1.46 earth log): the eye image 2496x2688, its half
// size buffer, the 8192x8192 shadow atlas and a 1248x2688 target that is not in the eye's space.
constexpr evr::foveation::TargetSize kEye{2496, 2688};

BeginShape eyePass(std::uint32_t width, std::uint32_t height) {
    BeginShape b;
    b.eye = 0;
    b.renderArea = {{0, 0}, {width, height}};
    b.framebuffer = {width, height};
    b.eyeImage = kEye;
    return b;
}

VkAttachmentDescription attachment(VkFormat format) {
    VkAttachmentDescription a{};
    a.format = format;
    a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return a;
}

// One graphics subpass: two colour attachments and a depth/stencil one, the depth also read as an input
// attachment (a deferred lighting pass's shape).
struct PassFixture {
    VkAttachmentDescription attachments[3] = {attachment(VK_FORMAT_R16G16B16A16_SFLOAT),
                                              attachment(VK_FORMAT_B8G8R8A8_UNORM),
                                              attachment(VK_FORMAT_D32_SFLOAT_S8_UINT)};
    VkAttachmentReference colors[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                       {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    VkAttachmentReference depth{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkAttachmentReference inputs[2] = {{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
                                       {0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkSubpassDescription subpass{};
    VkSubpassDependency dependency{VK_SUBPASS_EXTERNAL,
                                   0,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
                                   VK_DEPENDENCY_BY_REGION_BIT};
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};

    PassFixture() {
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.inputAttachmentCount = 2;
        subpass.pInputAttachments = inputs;
        subpass.colorAttachmentCount = 2;
        subpass.pColorAttachments = colors;
        subpass.pDepthStencilAttachment = &depth;
        info.attachmentCount = 3;
        info.pAttachments = attachments;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;
    }
};

} // namespace

TEST_CASE("vrs_khr: rate texels are the extension's 1x1, 2x2 and 4x4 values") {
    using evr::foveation::kRateFull;
    using evr::foveation::kRateHalf;
    using evr::foveation::kRateQuarter;
    const std::vector<std::uint8_t> pattern{kRateFull, kRateHalf, kRateQuarter, kRateHalf, kRateFull};
    CHECK(khr::encodeRates(pattern, true) == std::vector<std::uint8_t>{0x00, 0x05, 0x0A, 0x05, 0x00});
}

TEST_CASE("vrs_khr: quarter rate is 2x2 where the device's largest fragment is under 4x4") {
    using evr::foveation::kRateFull;
    using evr::foveation::kRateHalf;
    using evr::foveation::kRateQuarter;
    const std::vector<std::uint8_t> pattern{kRateQuarter, kRateHalf, kRateFull, kRateQuarter};
    CHECK(khr::encodeRates(pattern, false) == std::vector<std::uint8_t>{0x05, 0x05, 0x00, 0x05});
    CHECK(khr::encodeRates({}, false).empty());
}

TEST_CASE("vrs_khr: the rate image covers the framebuffer, rounded up to whole texels") {
    const VkExtent2D t16 = khr::rateTexels({2496, 2688}, {16, 16});
    CHECK(t16.width == 156);
    CHECK(t16.height == 168);
    const VkExtent2D t8 = khr::rateTexels({1449, 1559}, {8, 8});
    CHECK(t8.width == 182);
    CHECK(t8.height == 195);
}

TEST_CASE("vrs_khr: attachment aspects follow the format") {
    CHECK(khr::attachmentAspects(VK_FORMAT_D32_SFLOAT) == VK_IMAGE_ASPECT_DEPTH_BIT);
    CHECK(khr::attachmentAspects(VK_FORMAT_D16_UNORM) == VK_IMAGE_ASPECT_DEPTH_BIT);
    CHECK(khr::attachmentAspects(VK_FORMAT_S8_UINT) == VK_IMAGE_ASPECT_STENCIL_BIT);
    CHECK(khr::attachmentAspects(VK_FORMAT_D24_UNORM_S8_UINT) ==
          (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
    CHECK(khr::attachmentAspects(VK_FORMAT_D32_SFLOAT_S8_UINT) ==
          (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
    CHECK(khr::attachmentAspects(VK_FORMAT_B8G8R8A8_UNORM) == VK_IMAGE_ASPECT_COLOR_BIT);
    CHECK(khr::attachmentAspects(VK_FORMAT_R8_UINT) == VK_IMAGE_ASPECT_COLOR_BIT);
}

TEST_CASE("vrs_khr: one graphics subpass with nothing chained is promoted") {
    PassFixture f;
    CHECK(khr::promotes(f.info));
}

TEST_CASE("vrs_khr: a pass with a chained struct, several subpasses or a compute subpass is not") {
    PassFixture chained;
    VkRenderPassMultiviewCreateInfo multiview{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};
    chained.info.pNext = &multiview;
    CHECK_FALSE(khr::promotes(chained.info));

    PassFixture two;
    const VkSubpassDescription subpasses[2] = {two.subpass, two.subpass};
    two.info.subpassCount = 2;
    two.info.pSubpasses = subpasses;
    CHECK_FALSE(khr::promotes(two.info));

    PassFixture compute;
    compute.subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    CHECK_FALSE(khr::promotes(compute.info));
}

TEST_CASE("vrs_khr: the promoted pass appends a loaded and stored R8_UINT rate attachment") {
    PassFixture f;
    const khr::PromotedPass p(f.info, {16, 16});
    const VkRenderPassCreateInfo2& info = p.info();
    REQUIRE(info.attachmentCount == 4);
    CHECK(p.rateAttachment() == 3);
    const VkAttachmentDescription2& rate = info.pAttachments[3];
    CHECK(rate.format == VK_FORMAT_R8_UINT);
    CHECK(rate.samples == VK_SAMPLE_COUNT_1_BIT);
    CHECK(rate.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD);
    CHECK(rate.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
    CHECK(rate.initialLayout == VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR);
    CHECK(rate.finalLayout == VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR);
    for (std::uint32_t i = 0; i < 3; ++i) {
        CHECK(info.pAttachments[i].format == f.attachments[i].format);
        CHECK(info.pAttachments[i].loadOp == f.attachments[i].loadOp);
        CHECK(info.pAttachments[i].finalLayout == f.attachments[i].finalLayout);
    }
    REQUIRE(info.subpassCount == 1);
    const auto* rateInfo =
        static_cast<const VkFragmentShadingRateAttachmentInfoKHR*>(info.pSubpasses[0].pNext);
    REQUIRE(rateInfo != nullptr);
    CHECK(rateInfo->sType == VK_STRUCTURE_TYPE_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR);
    CHECK(rateInfo->pFragmentShadingRateAttachment->attachment == 3);
    CHECK(rateInfo->pFragmentShadingRateAttachment->layout ==
          VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR);
    CHECK(rateInfo->shadingRateAttachmentTexelSize.width == 16);
    CHECK(rateInfo->shadingRateAttachmentTexelSize.height == 16);
    REQUIRE(info.dependencyCount == 1);
    CHECK(info.pDependencies[0].srcSubpass == VK_SUBPASS_EXTERNAL);
    CHECK(info.pDependencies[0].dstAccessMask == VK_ACCESS_INPUT_ATTACHMENT_READ_BIT);
    CHECK(info.pDependencies[0].dependencyFlags == VK_DEPENDENCY_BY_REGION_BIT);
}

TEST_CASE("vrs_khr: the promoted pass keeps the subpass's references, input aspects from the format") {
    PassFixture f;
    const khr::PromotedPass p(f.info, {8, 8});
    const VkSubpassDescription2& sub = p.info().pSubpasses[0];
    REQUIRE(sub.inputAttachmentCount == 2);
    CHECK(sub.pInputAttachments[0].attachment == 2);
    CHECK(sub.pInputAttachments[0].aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
    CHECK(sub.pInputAttachments[1].attachment == 0);
    CHECK(sub.pInputAttachments[1].aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
    REQUIRE(sub.colorAttachmentCount == 2);
    CHECK(sub.pColorAttachments[1].attachment == 1);
    CHECK(sub.pResolveAttachments == nullptr);
    REQUIRE(sub.pDepthStencilAttachment != nullptr);
    CHECK(sub.pDepthStencilAttachment->attachment == 2);
    CHECK(sub.pDepthStencilAttachment->layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    CHECK(sub.viewMask == 0);
}

TEST_CASE("vrs_khr: an eye's pass over its whole framebuffer gets the eye's rate image") {
    CHECK(khr::rateFor(eyePass(2496, 2688)) == BeginRate::Eye);
    BeginShape half = eyePass(1248, 1344);
    half.eye = 1;
    CHECK(khr::rateFor(half) == BeginRate::Eye);
}

TEST_CASE("vrs_khr: mono frames, small or offset areas and the GUI target keep full rate") {
    BeginShape mono = eyePass(2496, 2688);
    mono.eye = khr::kNoEye;
    CHECK_FALSE(khr::asksGuiTarget(mono));
    CHECK(khr::rateFor(mono) == BeginRate::NoEye);
    CHECK(khr::rateFor(eyePass(255, 2688)) == BeginRate::Small);
    BeginShape offset = eyePass(2496, 2688);
    offset.renderArea.offset = {16, 0};
    CHECK(khr::rateFor(offset) == BeginRate::Offset);
    BeginShape gui = eyePass(2496, 2688);
    CHECK(khr::asksGuiTarget(gui));
    gui.guiTarget = true;
    CHECK(khr::rateFor(gui) == BeginRate::GuiTarget);
}

TEST_CASE("vrs_khr: a target not in the eye's space keeps full rate") {
    CHECK(khr::rateFor(eyePass(8192, 8192)) == BeginRate::OtherTarget);
    CHECK(khr::rateFor(eyePass(1248, 2688)) == BeginRate::OtherTarget);
    BeginShape unknownEye = eyePass(2496, 2688);
    unknownEye.eyeImage = {};
    CHECK(khr::rateFor(unknownEye) == BeginRate::OtherTarget);
}

TEST_CASE("vrs_khr: uniform mode rates every eligible target, as vrs_nv, but not the GUI target") {
    BeginShape shadow = eyePass(8192, 8192);
    shadow.uniform = true;
    CHECK(khr::rateFor(shadow) == BeginRate::Eye);
    BeginShape gui = eyePass(2496, 2688);
    gui.uniform = true;
    gui.guiTarget = true;
    CHECK(khr::rateFor(gui) == BeginRate::GuiTarget);
    BeginShape small = eyePass(128, 128);
    small.uniform = true;
    CHECK(khr::rateFor(small) == BeginRate::Small);
}

TEST_CASE("vrs_khr: secondary command buffers keep the game's framebuffer, so full rate") {
    BeginShape b = eyePass(2496, 2688);
    b.secondary = true;
    CHECK(khr::rateFor(b) == BeginRate::Secondary);
}

TEST_CASE("vrs_khr: a layered framebuffer keeps full rate") {
    BeginShape b = eyePass(2496, 2688);
    b.layers = 2;
    CHECK(khr::rateFor(b) == BeginRate::Layered);
}

TEST_CASE("vrs_khr: a render area smaller than its framebuffer keeps full rate") {
    // The eye's rate image is made for the render area; the framebuffer's needs ceil(framebuffer / texel).
    BeginShape b = eyePass(1248, 1344);
    b.framebuffer = {2496, 2688};
    CHECK(khr::rateFor(b) == BeginRate::PartialArea);
}
