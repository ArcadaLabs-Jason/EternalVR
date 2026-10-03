// Foveated rendering's settings and rate images (vrs_nv.hpp): the pattern for each render target size and
// eye, made and uploaded once.

#include "vkcore/vrs_nv.hpp"

#include "features/foveation/foveation_preset.hpp"
#include "features/foveation/foveation_region.hpp"
#include "features/foveation/rate_pattern.hpp"
#include "vkcore/log.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/vrs_nv_impl.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

namespace evr::vkcore::vrs_nv {

namespace {

// Rate images kept per device, one per render target size and eye.
constexpr std::size_t kMaxImages = 32;
// A rate image is used from its first command buffer on; other command buffers wait this long, so the one
// that uploads it has run.
constexpr double kReadySeconds = 1.0;

Settings readRequested() {
    Settings out;
    std::wstring value;
    out.marks = readEnv(L"ETERNALVR_VRS_TINT", value) && value == L"1";
    out.guesses = readEnv(L"ETERNALVR_TEST_VRS_PARITY", value) && value == L"1";
    if (!readEnv(L"ETERNALVR_VRS_TEST", value) || value.empty()) {
        // The player's setting (the launcher's Foveated rendering row): a preset's full-rate angle, half rate
        // for the preset's band more.
        if (!readEnv(L"ETERNALVR_FOVEATION", value) || value.empty()) {
            return out;
        }
        std::string text;
        for (const wchar_t c : value) {
            text.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
        const auto preset = foveation::parseFoveationPreset(text);
        const auto full = preset ? foveation::fullRateHalfAngleDegrees(*preset) : std::nullopt;
        const auto band = preset ? foveation::halfRateBandDegrees(*preset) : std::nullopt;
        if (!preset) {
            EVR_LOG("vrs: ETERNALVR_FOVEATION '%ls' is not off, subtle, balanced, aggressive or maximum; off",
                    value.c_str());
        }
        if (full && band) {
            out.mode = Mode::Fovea;
            out.fullDegrees = *full;
            out.halfDegrees = *full + *band;
        }
        return out;
    }
    if (value == L"2x2" || value == L"4x4") {
        out.mode = Mode::Uniform;
        out.rate = value == L"2x2" ? foveation::kRateHalf : foveation::kRateQuarter;
    } else if (value == L"eyetest") {
        out.mode = Mode::EyeTest;
    } else if (value == L"fovea") {
        out.mode = Mode::Fovea;
        std::wstring angles;
        float full = 0.0f;
        float half = 0.0f;
        if (readEnv(L"ETERNALVR_VRS_FOVEA", angles) &&
            swscanf_s(angles.c_str(), L"%f,%f", &full, &half) == 2 && full > 0.0f && half > full &&
            half < 89.0f) {
            out.fullDegrees = full;
            out.halfDegrees = half;
        }
    } else {
        EVR_LOG("vrs: ETERNALVR_VRS_TEST '%ls' is not 2x2, 4x4, eyetest or fovea; off", value.c_str());
    }
    return out;
}

// Parallel Eye Rendering renders both eyes as two views of one frame, with no eye tag per backend frame for
// the passes: foveation would keep every pass at full rate, so it is off (the launcher does not offer both).
// By the request on the build it supports, whether or not it then installs: settings() can first run before
// vkCreateInstance's install. On other builds foveation stays on.
Settings readSettings() {
    const Settings requested = readRequested();
    if (requested.mode != Mode::Off && parallelEyesRequested()) {
        EVR_LOG("vrs: foveated rendering is off: Parallel Eye Rendering is requested "
                "(ETERNALVR_PARALLEL_EYES=1 on its build)");
        return {};
    }
    return requested;
}

// Each eye's FOV and orientation in the head, published by the presenter.
struct EyeShape {
    xr_math::Fov fov;
    Quat orientation;
};
std::mutex& g_eyesMutex = *new std::mutex;
std::array<std::optional<EyeShape>, 2> g_eyes;

// Creates the rate image for `pattern` and records its upload into `commandBuffer` (outside a render pass).
bool createAndUpload(VrsDevice& d,
                     VkCommandBuffer commandBuffer,
                     VkExtent2D texels,
                     const std::vector<std::uint8_t>& pattern,
                     RateImage& out) {
    DeviceData& data = *d.data;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8_UINT;
    info.extent = {texels.width, texels.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (data.vk.CreateImage(data.device, &info, nullptr, &out.image) != VK_SUCCESS) {
        return false;
    }
    VkMemoryRequirements req{};
    data.vk.GetImageMemoryRequirements(data.device, out.image, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memoryType(data, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (alloc.memoryTypeIndex == UINT32_MAX ||
        data.vk.AllocateMemory(data.device, &alloc, nullptr, &out.memory) != VK_SUCCESS ||
        data.vk.BindImageMemory(data.device, out.image, out.memory, 0) != VK_SUCCESS) {
        return false;
    }
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = out.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R8_UINT;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (d.createImageView(data.device, &view, nullptr, &out.view) != VK_SUCCESS) {
        return false;
    }
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size = pattern.size();
    buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (data.vk.CreateBuffer(data.device, &buffer, nullptr, &out.staging) != VK_SUCCESS) {
        return false;
    }
    data.vk.GetBufferMemoryRequirements(data.device, out.staging, &req);
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memoryType(
        data, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped = nullptr;
    if (alloc.memoryTypeIndex == UINT32_MAX ||
        data.vk.AllocateMemory(data.device, &alloc, nullptr, &out.stagingMemory) != VK_SUCCESS ||
        data.vk.BindBufferMemory(data.device, out.staging, out.stagingMemory, 0) != VK_SUCCESS ||
        data.vk.MapMemory(data.device, out.stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
        return false;
    }
    std::memcpy(mapped, pattern.data(), pattern.size());
    data.vk.UnmapMemory(data.device, out.stagingMemory);

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = out.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    data.vk.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                               0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {texels.width, texels.height, 1};
    d.cmdCopyBufferToImage(commandBuffer, out.staging, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &copy);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADING_RATE_IMAGE_READ_BIT_NV;
    data.vk.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_SHADING_RATE_IMAGE_BIT_NV, 0, 0, nullptr, 0, nullptr, 1,
                               &barrier);
    out.uploadedBy = commandBuffer;
    out.uploadedAt = nowSeconds();
    return true;
}

} // namespace

const Settings& settings() {
    static const Settings s = readSettings();
    return s;
}

std::uint32_t memoryType(DeviceData& data, std::uint32_t bits, VkMemoryPropertyFlags wanted) {
    VkPhysicalDeviceMemoryProperties props{};
    data.instance->vk.GetPhysicalDeviceMemoryProperties(data.physicalDevice, &props);
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & wanted) == wanted) {
            return i;
        }
    }
    return UINT32_MAX;
}

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void noteEye(int eye, const xr_math::Fov& fov, const Quat& orientationInHead) {
    if (settings().mode != Mode::Fovea || (eye != 0 && eye != 1)) {
        return;
    }
    std::lock_guard lock(g_eyesMutex);
    auto& slot = g_eyes[static_cast<std::size_t>(eye)];
    if (!slot) {
        slot = EyeShape{fov, orientationInHead};
    }
}

std::vector<std::uint8_t> patternFor(VkExtent2D texel, VkExtent2D extent, int eye, bool log) {
    const foveation::RatePatternSize size{extent.width, extent.height, texel.width, texel.height};
    const Settings& s = settings();
    if (s.mode == Mode::EyeTest) {
        return foveation::eyeTestPattern(size, eye);
    }
    if (s.mode == Mode::Uniform) {
        std::vector<std::uint8_t> out = foveation::eyeTestPattern(size, eye);
        std::fill(out.begin(), out.end(), s.rate);
        return out;
    }
    std::optional<EyeShape> shape;
    {
        std::lock_guard lock(g_eyesMutex);
        shape = g_eyes[static_cast<std::size_t>(eye)];
    }
    if (!shape) {
        return {};
    }
    const auto full = foveation::foveationRegion(shape->fov, shape->orientation, s.fullDegrees);
    const auto half = foveation::foveationRegion(shape->fov, shape->orientation, s.halfDegrees);
    if (!full || !half) {
        return {};
    }
    if (log) {
        EVR_LOG("vrs: eye %d, %ux%u: centre %.2f %.2f; full rate within the %.0f deg region (radii left %.2f "
                "right %.2f top %.2f bottom %.2f), half rate within the %.0f deg one (%.2f %.2f %.2f %.2f), "
                "quarter rate outside",
                eye, extent.width, extent.height, full->centerX, full->centerY, s.fullDegrees,
                full->radiusLeft, full->radiusRight, full->radiusTop, full->radiusBottom, s.halfDegrees,
                half->radiusLeft, half->radiusRight, half->radiusTop, half->radiusBottom);
    }
    return foveation::foveatedPattern(size, *full, *half);
}

void noteOtherTarget(VrsDevice& d, VkExtent2D extent, foveation::TargetSize eye) {
    constexpr std::size_t kLoggedSizes = 16;
    if (d.otherSizesFull.load(std::memory_order_relaxed)) {
        return;
    }
    std::lock_guard lock(d.otherMutex);
    const std::uint64_t key = packed(extent);
    if (std::find(d.otherSizes.begin(), d.otherSizes.end(), key) != d.otherSizes.end()) {
        return;
    }
    d.otherSizes.push_back(key);
    d.otherSizesFull.store(d.otherSizes.size() >= kLoggedSizes, std::memory_order_relaxed);
    EVR_LOG("vrs: render target %ux%u is not in the eye's space (eye image %ux%u); it keeps full rate",
            extent.width, extent.height, eye.width, eye.height);
}

VkImageView viewFor(VrsDevice& d, VkCommandBuffer commandBuffer, VkExtent2D extent, int eye) {
    const std::uint64_t key = (static_cast<std::uint64_t>(extent.width) << 33) |
                              (static_cast<std::uint64_t>(extent.height) << 1) |
                              static_cast<std::uint64_t>(eye);
    std::lock_guard lock(d.imagesMutex);
    if (const auto it = d.images.find(key); it != d.images.end()) {
        const RateImage& image = it->second;
        const bool ready =
            image.uploadedBy == commandBuffer || nowSeconds() - image.uploadedAt > kReadySeconds;
        return ready ? image.view : VK_NULL_HANDLE;
    }
    if (d.images.size() >= kMaxImages) {
        if (!d.imagesFull) {
            d.imagesFull = true;
            EVR_LOG("vrs: %zu rate images made; render targets of new sizes keep full rate", kMaxImages);
        }
        return VK_NULL_HANDLE;
    }
    const std::vector<std::uint8_t> pattern = patternFor(d.texel, extent, eye, true);
    if (pattern.empty()) {
        return VK_NULL_HANDLE; // the eye's shape is not known yet: asked again at the next pass
    }
    const VkExtent2D texels{(extent.width + d.texel.width - 1) / d.texel.width,
                            (extent.height + d.texel.height - 1) / d.texel.height};
    RateImage image;
    if (!createAndUpload(d, commandBuffer, texels, pattern, image)) {
        EVR_LOG("vrs: the rate image for %ux%u could not be made; that size keeps full rate", extent.width,
                extent.height);
    }
    d.images[key] = image; // kept even when it failed, so it is not tried again
    return image.view;
}

} // namespace evr::vkcore::vrs_nv
