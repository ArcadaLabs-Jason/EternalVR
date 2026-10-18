// Foveated rendering through VK_KHR_fragment_shading_rate (vrs_khr.hpp): the neutral rate images, all full
// rate, in the framebuffers of the promoted render passes.
//
// A framebuffer holds its rate image from its creation on, so every begin of it uses the neutral image,
// whichever command buffer records it. Its texels must be zero (1x1) and it must be in the attachment's
// layout before any of them runs. As for an eye's rate image (vrs_rate_images.cpp), the clear is recorded
// into each command buffer that begins a pass with it, outside the pass, until one that did has had
// kReadySeconds to run; the clear starts from UNDEFINED after the attachment's earlier reads, so recording it
// again in a later command buffer is harmless. The layer has no queue of its own to clear it once at
// creation: the game's queues are its own to synchronize.

#include "vkcore/log.hpp"
#include "vkcore/vrs_khr_impl.hpp"

#include <cstdint>
#include <mutex>

namespace evr::vkcore::vrs_khr {

namespace {

constexpr double kReadySeconds = 1.0; // vrs_rate_images.cpp's

void destroy(VrsDevice& d, const NeutralImage& n) {
    DeviceData& data = *d.data;
    if (n.view != VK_NULL_HANDLE) {
        d.destroyImageView(data.device, n.view, nullptr);
    }
    if (n.image != VK_NULL_HANDLE) {
        data.vk.DestroyImage(data.device, n.image, nullptr);
    }
    if (n.memory != VK_NULL_HANDLE) {
        data.vk.FreeMemory(data.device, n.memory, nullptr);
    }
}

// Makes the image, its memory and its view; false (with what was made freed) when one fails.
bool make(VrsDevice& d, VkExtent2D texels, NeutralImage& out) {
    DeviceData& data = *d.data;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8_UINT;
    info.extent = {texels.width, texels.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool ok = data.vk.CreateImage(data.device, &info, nullptr, &out.image) == VK_SUCCESS;
    if (ok) {
        VkMemoryRequirements req{};
        data.vk.GetImageMemoryRequirements(data.device, out.image, &req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex =
            vrs_nv::memoryType(data, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ok = alloc.memoryTypeIndex != UINT32_MAX &&
             data.vk.AllocateMemory(data.device, &alloc, nullptr, &out.memory) == VK_SUCCESS &&
             data.vk.BindImageMemory(data.device, out.image, out.memory, 0) == VK_SUCCESS;
    }
    if (ok) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = out.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_R8_UINT;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        ok = d.createImageView(data.device, &view, nullptr, &out.view) == VK_SUCCESS;
    }
    if (!ok) {
        destroy(d, out);
        out = {};
    }
    return ok;
}

} // namespace

VkImageView neutralView(VrsDevice& d, VkExtent2D texels) {
    KhrDevice& k = *d.khrState;
    const std::uint64_t key = vrs_nv::packed(texels);
    std::lock_guard lock(k.neutralMutex);
    if (const auto it = k.neutral.find(key); it != k.neutral.end()) {
        return it->second.view;
    }
    NeutralImage image;
    if (!make(d, texels, image)) {
        EVR_LOG("vrs: the full-rate rate image of %ux%u texels could not be made", texels.width,
                texels.height);
        return VK_NULL_HANDLE;
    }
    k.neutral.emplace(key, image);
    if (k.neutral.size() == 1) {
        EVR_LOG("vrs: first full-rate rate image made (%ux%u texels); each command buffer clears it before "
                "its first use until one that did has had %.0f s to run",
                texels.width, texels.height, kReadySeconds);
    }
    return image.view;
}

void prepareNeutral(VrsDevice& d, VkCommandBuffer commandBuffer, std::uint64_t key) {
    KhrDevice& k = *d.khrState;
    DeviceData& data = *d.data;
    std::lock_guard lock(k.neutralMutex);
    const auto it = k.neutral.find(key);
    if (it == k.neutral.end()) {
        return;
    }
    NeutralImage& n = it->second;
    const double now = vrs_nv::nowSeconds();
    if (n.firstClearedAt != 0.0 && (n.clearedBy == commandBuffer || now - n.firstClearedAt > kReadySeconds)) {
        return;
    }
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = n.image;
    barrier.subresourceRange = range;
    // After any earlier pass's reads of it (an earlier clear's command buffer may already have run).
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    data.vk.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR,
                               VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    const VkClearColorValue zero{};
    k.cmdClearColorImage(commandBuffer, n.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;
    data.vk.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR, 0, 0, nullptr, 0,
                               nullptr, 1, &barrier);
    n.clearedBy = commandBuffer;
    if (n.firstClearedAt == 0.0) {
        n.firstClearedAt = now;
    }
}

} // namespace evr::vkcore::vrs_khr
