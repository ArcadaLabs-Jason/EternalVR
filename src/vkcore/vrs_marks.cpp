// The reduced-rate areas marked in the headset (vrs_nv.hpp, ETERNALVR_VRS_TINT=1): after the presenter copies
// an eye's image into the ring, a 4x4 dot is copied onto it at the centre of every second rate texel each way
// that is not at full rate, yellow at half rate, red at quarter rate. Copies only, no shaders: the dots come
// from a small two-layer image of the ring's format, cleared once per queue family. The pattern is the one
// the eye image's own size gets (vrs_rate_images.cpp), in the same NDC, so it lines up whatever size the
// ring's eye has. It is the eye's pattern, not each pass's rate: an eye image is marked only when some of
// that eye's render passes got a rate image since its previous marks (none while every pass is full rate,
// with no frame known), and full-rate passes (the GUI target, other targets) are dotted where they draw. The
// eye captures and the desktop mirror read the game's image and show no marks.

#include "features/foveation/rate_pattern.hpp"
#include "vkcore/log.hpp"
#include "vkcore/vrs_nv.hpp"
#include "vkcore/vrs_nv_impl.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <tuple>
#include <utility>
#include <vector>

namespace evr::vkcore::vrs_nv {

namespace {

constexpr std::uint32_t kDot = 4;    // a dot's side, in pixels
constexpr std::uint32_t kStride = 2; // a dot on every second rate texel each way
constexpr VkClearColorValue kHalfColour{{1.0f, 0.85f, 0.0f, 1.0f}};
constexpr VkClearColorValue kQuarterColour{{1.0f, 0.1f, 0.1f, 1.0f}};
// The dot image is used from the command buffer that clears it on; others wait this long for it to have run.
constexpr double kReadySeconds = 1.0;

struct DotImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkCommandBuffer clearedBy = VK_NULL_HANDLE;
    double clearedAt = 0.0;
};

std::mutex& g_mutex = *new std::mutex;
// By device, format and queue family; a failed one is kept (VK_NULL_HANDLE) so it is not tried again.
auto& g_dots = *new std::map<std::tuple<VkDevice, VkFormat, std::uint32_t>, DotImage>;
// By eye shape generation, eye, size and offset: the copies for one eye image.
auto& g_regions =
    *new std::map<std::tuple<std::uint32_t, int, std::uint32_t, std::uint32_t, std::int32_t, std::int32_t>,
                  std::vector<VkImageCopy>>;

// Under g_mutex: the dot image for `format` on `family`, cleared into `commandBuffer` the first time;
// VK_NULL_HANDLE while it cannot be made or is not ready.
VkImage dotImage(DeviceData& dev, VkCommandBuffer commandBuffer, std::uint32_t family, VkFormat format) {
    const auto key = std::make_tuple(dev.device, format, family);
    if (const auto it = g_dots.find(key); it != g_dots.end()) {
        const DotImage& dot = it->second;
        const bool ready = dot.clearedBy == commandBuffer || nowSeconds() - dot.clearedAt > kReadySeconds;
        return ready ? dot.image : VK_NULL_HANDLE;
    }
    DotImage& dot = g_dots[key];
    VkFormatProperties props{};
    dev.instance->vk.GetPhysicalDeviceFormatProperties(dev.physicalDevice, format, &props);
    const VkFormatFeatureFlags needed =
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {kDot, kDot, 1};
    info.mipLevels = 1;
    info.arrayLayers = 2;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkMemoryRequirements req{};
    if ((props.optimalTilingFeatures & needed) != needed ||
        dev.vk.CreateImage(dev.device, &info, nullptr, &image) != VK_SUCCESS) {
        EVR_LOG("vrs: the marks' image for format %d could not be made; no marks", static_cast<int>(format));
        return VK_NULL_HANDLE;
    }
    dev.vk.GetImageMemoryRequirements(dev.device, image, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memoryType(dev, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (alloc.memoryTypeIndex == UINT32_MAX ||
        dev.vk.AllocateMemory(dev.device, &alloc, nullptr, &memory) != VK_SUCCESS ||
        dev.vk.BindImageMemory(dev.device, image, memory, 0) != VK_SUCCESS) {
        EVR_LOG("vrs: no memory for the marks' image; no marks");
        if (memory) {
            dev.vk.FreeMemory(dev.device, memory, nullptr);
        }
        dev.vk.DestroyImage(dev.device, image, nullptr);
        return VK_NULL_HANDLE;
    }
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dev.vk.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    const VkImageSubresourceRange half{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    const VkImageSubresourceRange quarter{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1, 1};
    dev.vk.CmdClearColorImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &kHalfColour, 1,
                              &half);
    dev.vk.CmdClearColorImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &kQuarterColour, 1,
                              &quarter);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    dev.vk.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0, 0, nullptr, 0, nullptr, 1, &barrier);
    dot = DotImage{image, memory, commandBuffer, nowSeconds()};
    EVR_LOG("vrs: marks' image made for format %d on queue family %u", static_cast<int>(format), family);
    return image;
}

// Under g_mutex: the dot copies for eye `eye`'s image of `extent` at `offset`; empty while its pattern is not
// known (asked again at the next copy).
const std::vector<VkImageCopy>& regionsFor(VkExtent2D texel, int eye, VkOffset2D offset, VkExtent2D extent) {
    static const std::vector<VkImageCopy> kNone;
    const auto key =
        std::make_tuple(eyeShapeGeneration(), eye, extent.width, extent.height, offset.x, offset.y);
    if (const auto it = g_regions.find(key); it != g_regions.end()) {
        return it->second;
    }
    const std::vector<std::uint8_t> pattern = patternFor(texel, extent, eye, false);
    if (pattern.empty()) {
        return kNone;
    }
    const std::uint32_t columns = (extent.width + texel.width - 1) / texel.width;
    const std::uint32_t rows = (extent.height + texel.height - 1) / texel.height;
    std::vector<VkImageCopy> copies;
    for (std::uint32_t ty = 0; ty < rows; ty += kStride) {
        for (std::uint32_t tx = 0; tx < columns; tx += kStride) {
            const std::uint8_t rate = pattern[static_cast<std::size_t>(ty) * columns + tx];
            const std::uint32_t x = tx * texel.width + texel.width / 2 - kDot / 2;
            const std::uint32_t y = ty * texel.height + texel.height / 2 - kDot / 2;
            if (rate == foveation::kRateFull || x + kDot > extent.width || y + kDot > extent.height) {
                continue;
            }
            VkImageCopy copy{};
            copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, rate == foveation::kRateHalf ? 0u : 1u, 1};
            copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.dstOffset = {offset.x + static_cast<std::int32_t>(x),
                              offset.y + static_cast<std::int32_t>(y), 0};
            copy.extent = {kDot, kDot, 1};
            copies.push_back(copy);
        }
    }
    EVR_LOG("vrs: marks for eye %d, %ux%u: %zu dot(s)", eye, extent.width, extent.height, copies.size());
    return g_regions[key] = std::move(copies);
}

} // namespace

bool marksWanted() {
    return settings().marks && settings().mode != Mode::Off;
}

void recordMarks(DeviceData& dev,
                 VkCommandBuffer commandBuffer,
                 std::uint32_t family,
                 const std::array<VkImage, 2>& images,
                 VkFormat format,
                 int eye,
                 VkOffset2D offset,
                 VkExtent2D extent) {
    VrsDevice* d = marksWanted() ? deviceOf(keyOf(dev.device)) : nullptr;
    const VkQueueFlags flags = family < dev.queueFamilyFlags.size() ? dev.queueFamilyFlags[family] : 0;
    if (!d || !d->on || (eye != 0 && eye != 1) || !(flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))) {
        return;
    }
    // With one rate for every pass, every pass counts as eye L's.
    const auto index = static_cast<std::size_t>(eye);
    const std::uint64_t coarse = d->coarse[settings().mode == Mode::Uniform ? 0 : index].load();
    if (d->coarseMarked[index].exchange(coarse) == coarse) {
        return; // none of the eye's passes got a rate image since its previous marks
    }
    std::lock_guard lock(g_mutex);
    const std::vector<VkImageCopy>& copies = regionsFor(d->texel, eye, offset, extent);
    const VkImage dot = copies.empty() ? VK_NULL_HANDLE : dotImage(dev, commandBuffer, family, format);
    if (!dot) {
        return;
    }
    // After the eye's own copy into the same image.
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dev.vk.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0, 1, &barrier, 0, nullptr, 0, nullptr);
    for (const VkImage image : images) {
        if (image) {
            dev.vk.CmdCopyImage(commandBuffer, dot, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                static_cast<std::uint32_t>(copies.size()), copies.data());
        }
    }
}

} // namespace evr::vkcore::vrs_nv
