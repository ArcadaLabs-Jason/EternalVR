// Variable rate shading experiment (vrs_nv.hpp).

#include "vkcore/vrs_nv.hpp"

#include "features/foveation/foveation_preset.hpp"
#include "features/foveation/foveation_region.hpp"
#include "features/foveation/rate_pattern.hpp"
#include "vkcore/log.hpp"
#include "vkcore/seq_hooks.hpp"
#include "vkcore/ui_vulkan.hpp"
#include "vkcore/vrs_gui.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace evr::vkcore::vrs_nv {

namespace {

using DispatchKey = void*;

template <typename Handle>
DispatchKey keyOf(Handle handle) {
    return *reinterpret_cast<void**>(handle);
}

// Palette entries: a rate image texel's value picks one (foveation::kRateFull, kRateHalf, kRateQuarter).
constexpr VkShadingRatePaletteEntryNV kPalette[] = {
    VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_PIXEL_NV,
    VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X2_PIXELS_NV,
    VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X4_PIXELS_NV,
};

// Render passes smaller than this (in either direction) keep full rate: shadow cascades, small effects.
constexpr std::uint32_t kMinTarget = 256;
// Rate images kept per device, one per render target size and eye.
constexpr std::size_t kMaxImages = 32;
// A rate image is used from its first command buffer on; other command buffers wait this long, so the one
// that uploads it has run.
constexpr double kReadySeconds = 1.0;

enum class Mode { Off, Uniform, EyeTest, Fovea };

// The half-rate ring's width beyond a preset's full-rate angle; quarter rate outside it (degrees).
constexpr float kHalfRateBand = 16.0f;

struct Settings {
    Mode mode = Mode::Off;
    std::uint8_t rate = 0;     // Uniform: the palette index every texel holds
    float fullDegrees = 24.0f; // Fovea: the full-rate half-angle around head-forward
    float halfDegrees = 40.0f; // Fovea: the half-rate half-angle; quarter rate outside
};

const Settings& settings() {
    static const Settings s = [] {
        Settings out;
        std::wstring value;
        if (!readEnv(L"ETERNALVR_VRS_TEST", value) || value.empty()) {
            // The player's setting (the launcher's Foveated rendering row): a preset's full-rate angle, half
            // rate for kHalfRateBand degrees more.
            if (!readEnv(L"ETERNALVR_FOVEATION", value) || value.empty()) {
                return out;
            }
            std::string text;
            for (const wchar_t c : value) {
                text.push_back(c < 0x80 ? static_cast<char>(c) : '?');
            }
            const auto preset = foveation::parseFoveationPreset(text);
            const auto full = preset ? foveation::fullRateHalfAngleDegrees(*preset) : std::nullopt;
            if (!preset) {
                EVR_LOG("vrs: ETERNALVR_FOVEATION '%ls' is not off, subtle, balanced or aggressive; off",
                        value.c_str());
            }
            if (full) {
                out.mode = Mode::Fovea;
                out.fullDegrees = *full;
                out.halfDegrees = *full + kHalfRateBand;
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
    }();
    return s;
}

// Each eye's FOV and orientation in the head, published by the presenter.
struct EyeShape {
    xr_math::Fov fov;
    Quat orientation;
};
std::mutex& g_eyesMutex = *new std::mutex;
std::array<std::optional<EyeShape>, 2> g_eyes;

struct RateImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkCommandBuffer uploadedBy = VK_NULL_HANDLE;
    double uploadedAt = 0.0;
};

struct VrsDevice {
    bool on = false; // the extension is enabled
    DeviceData* data = nullptr;
    VkExtent2D texel{16, 16};
    PFN_vkCreateGraphicsPipelines createGraphicsPipelines = nullptr;
    PFN_vkCmdBeginRenderPass cmdBeginRenderPass = nullptr;
    PFN_vkCmdBindShadingRateImageNV cmdBindShadingRateImage = nullptr;
    PFN_vkCreateImageView createImageView = nullptr;
    PFN_vkCmdCopyBufferToImage cmdCopyBufferToImage = nullptr;
    std::mutex imagesMutex;
    std::unordered_map<std::uint64_t, RateImage> images;
    bool imagesFull = false;
    std::atomic<std::uint64_t> pipelines{0};
    std::array<std::atomic<std::uint64_t>, 3> binds{}; // eye L, eye R, untagged
    std::atomic<std::uint64_t> passes{0};
    std::atomic<std::uint64_t> fullRate{0};
    std::atomic<std::uint64_t> guiPasses{0}; // passes into the GUI target, kept at full rate
};

// Allocated once and never destroyed (see layer_entry.cpp: no teardown at process exit).
std::shared_mutex& g_devicesMutex = *new std::shared_mutex;
auto& g_devices = *new std::unordered_map<DispatchKey, VrsDevice*>;

VkPhysicalDeviceShadingRateImageFeaturesNV g_feature{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_FEATURES_NV};

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

VrsDevice* deviceOf(DispatchKey key) {
    std::shared_lock lock(g_devicesMutex);
    const auto it = g_devices.find(key);
    return it == g_devices.end() ? nullptr : it->second;
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

// The texels for a render target of `extent` drawn for `eye`; empty when the eye's shape is not known yet.
std::vector<std::uint8_t> patternFor(const VrsDevice& d, VkExtent2D extent, int eye) {
    const foveation::RatePatternSize size{extent.width, extent.height, d.texel.width, d.texel.height};
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
    const auto full = foveation::fullRateRegion(shape->fov, shape->orientation, s.fullDegrees);
    const auto half = foveation::fullRateRegion(shape->fov, shape->orientation, s.halfDegrees);
    if (!full || !half) {
        return {};
    }
    EVR_LOG("vrs: eye %d, %ux%u: full rate within %.0f deg (centre %.2f %.2f, radii %.2f %.2f), half rate "
            "within %.0f deg (radii %.2f %.2f), quarter rate outside",
            eye, extent.width, extent.height, s.fullDegrees, full->centerX, full->centerY, full->radiusX,
            full->radiusY, s.halfDegrees, half->radiusX, half->radiusY);
    return foveation::foveatedPattern(size, *full, *half);
}

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

// The rate image for a render target of `extent` drawn for `eye`, or VK_NULL_HANDLE (full rate) while it is
// not ready. Made and uploaded in `commandBuffer` the first time.
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
    const std::vector<std::uint8_t> pattern = patternFor(d, extent, eye);
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

VKAPI_ATTR VkResult VKAPI_CALL CreateGraphicsPipelines(VkDevice device,
                                                       VkPipelineCache cache,
                                                       std::uint32_t count,
                                                       const VkGraphicsPipelineCreateInfo* pInfos,
                                                       const VkAllocationCallbacks* pAllocator,
                                                       VkPipeline* pPipelines) {
    VrsDevice* d = deviceOf(keyOf(device));
    if (!d->on) {
        return d->createGraphicsPipelines(device, cache, count, pInfos, pAllocator, pPipelines);
    }
    // Copies with a shading rate state chained into each viewport state; reserved so no pointer moves.
    std::vector<VkGraphicsPipelineCreateInfo> infos(pInfos, pInfos + count);
    std::vector<VkPipelineViewportStateCreateInfo> viewports(count);
    std::vector<VkPipelineViewportShadingRateImageStateCreateInfoNV> states(count);
    std::uint32_t most = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (pInfos[i].pViewportState) {
            most = std::max(most, pInfos[i].pViewportState->viewportCount);
        }
    }
    const VkShadingRatePaletteNV palette{static_cast<std::uint32_t>(std::size(kPalette)), kPalette};
    const std::vector<VkShadingRatePaletteNV> palettes(std::max(most, 1u), palette);
    std::uint64_t changed = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const VkPipelineViewportStateCreateInfo* original = pInfos[i].pViewportState;
        if (!original || original->viewportCount == 0) {
            continue;
        }
        viewports[i] = *original;
        states[i] = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_SHADING_RATE_IMAGE_STATE_CREATE_INFO_NV};
        states[i].pNext = original->pNext;
        states[i].shadingRateImageEnable = VK_TRUE;
        states[i].viewportCount = original->viewportCount;
        states[i].pShadingRatePalettes = palettes.data();
        viewports[i].pNext = &states[i];
        infos[i].pViewportState = &viewports[i];
        ++changed;
    }
    if (d->pipelines.fetch_add(changed) == 0 && changed > 0) {
        EVR_LOG("vrs: graphics pipelines get the shading rate palette (1x1, 2x2, 4x4)");
    }
    return d->createGraphicsPipelines(device, cache, count, infos.data(), pAllocator, pPipelines);
}

VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass(VkCommandBuffer commandBuffer,
                                              const VkRenderPassBeginInfo* pRenderPassBegin,
                                              VkSubpassContents contents) {
    VrsDevice* d = deviceOf(keyOf(commandBuffer));
    if (d->on) {
        const VkExtent2D extent = pRenderPassBegin->renderArea.extent;
        // The eye of the backend frame these commands belong to: the render thread records a frame while the
        // game's chain already runs the next one, so the chain's own eye (seqRenderEye) is the other eye.
        // Untagged frames (mono, or tags out of step) keep full rate unless every eye gets the same image.
        int eye = 2;
        if (settings().mode == Mode::Uniform) {
            eye = 0;
        } else if (const std::optional<stereo_seq::RenderTag> tag = seqTagInFlight()) {
            eye = stereo_seq::eyeIndex(tag->eye);
        }
        VkImageView view = VK_NULL_HANDLE;
        if (eye != 2 && extent.width >= kMinTarget && extent.height >= kMinTarget &&
            pRenderPassBegin->renderArea.offset.x == 0 && pRenderPassBegin->renderArea.offset.y == 0) {
            // Passes into the GUI target (the game's menus and HUD) keep full rate: their text turns coarse.
            if (!vrs_gui::drawsGuiTarget(commandBuffer, *pRenderPassBegin)) {
                view = viewFor(*d, commandBuffer, extent, eye);
            } else if (d->guiPasses.fetch_add(1) == 0) {
                EVR_LOG("vrs: first render pass into the GUI target (image %p, %ux%u) kept at full rate; the "
                        "game's menus and HUD are not foveated",
                        reinterpret_cast<void*>(ui_vulkan::guiTarget()), extent.width, extent.height);
            }
        }
        d->cmdBindShadingRateImage(commandBuffer, view, VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV);
        d->binds[static_cast<std::size_t>(eye)].fetch_add(1);
        const std::uint64_t full = view ? d->fullRate.load() : d->fullRate.fetch_add(1) + 1;
        if (d->passes.fetch_add(1) % 200000 == 199999) {
            EVR_LOG(
                "vrs: render passes: %llu recorded for eye L, %llu for eye R, %llu untagged; %llu of them at "
                "full rate (%llu into the GUI target); %llu pipeline(s) with the palette",
                static_cast<unsigned long long>(d->binds[0].load()),
                static_cast<unsigned long long>(d->binds[1].load()),
                static_cast<unsigned long long>(d->binds[2].load()), static_cast<unsigned long long>(full),
                static_cast<unsigned long long>(d->guiPasses.load()),
                static_cast<unsigned long long>(d->pipelines.load()));
        }
    }
    d->cmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
}

} // namespace

bool wanted() {
    return settings().mode != Mode::Off;
}

VkPhysicalDeviceShadingRateImageFeaturesNV*
planDevice(InstanceData& inst, VkPhysicalDevice physicalDevice, std::vector<const char*>& extensions) {
    if (!wanted()) {
        return nullptr;
    }
    std::uint32_t count = 0;
    inst.vk.EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    inst.vk.EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, available.data());
    const bool supported =
        std::any_of(available.begin(), available.end(), [](const VkExtensionProperties& p) {
            return std::strcmp(p.extensionName, VK_NV_SHADING_RATE_IMAGE_EXTENSION_NAME) == 0;
        });
    VkPhysicalDeviceShadingRateImageFeaturesNV query{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_FEATURES_NV};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &query};
    if (supported && inst.vk.GetPhysicalDeviceFeatures2) {
        inst.vk.GetPhysicalDeviceFeatures2(physicalDevice, &features);
    }
    if (!supported || !query.shadingRateImage) {
        EVR_LOG("vrs: %s is not supported on this device (NVIDIA RTX only); foveated rendering stays off",
                VK_NV_SHADING_RATE_IMAGE_EXTENSION_NAME);
        return nullptr;
    }
    extensions.push_back(VK_NV_SHADING_RATE_IMAGE_EXTENSION_NAME);
    g_feature.shadingRateImage = VK_TRUE;
    EVR_LOG("  adding device extension %s (foveated rendering)", VK_NV_SHADING_RATE_IMAGE_EXTENSION_NAME);
    return &g_feature;
}

void onDeviceCreated(DeviceData& data, bool enabled) {
    if (!wanted()) {
        return;
    }
    auto* d = new VrsDevice; // kept for the process's life, like the other device tables
    d->data = &data;
    const auto next = [&](const char* name) {
        return data.nextGetDeviceProcAddr(data.device, name);
    };
    d->createGraphicsPipelines =
        reinterpret_cast<PFN_vkCreateGraphicsPipelines>(next("vkCreateGraphicsPipelines"));
    d->cmdBeginRenderPass = reinterpret_cast<PFN_vkCmdBeginRenderPass>(next("vkCmdBeginRenderPass"));
    if (enabled) {
        const auto load = [&](const char* name) {
            return data.nextGetDeviceProcAddr(data.device, name);
        };
        d->cmdBindShadingRateImage =
            reinterpret_cast<PFN_vkCmdBindShadingRateImageNV>(load("vkCmdBindShadingRateImageNV"));
        d->createImageView = reinterpret_cast<PFN_vkCreateImageView>(load("vkCreateImageView"));
        d->cmdCopyBufferToImage =
            reinterpret_cast<PFN_vkCmdCopyBufferToImage>(load("vkCmdCopyBufferToImage"));
        VkPhysicalDeviceShadingRateImagePropertiesNV props{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_PROPERTIES_NV};
        VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &props};
        data.instance->vk.GetPhysicalDeviceProperties2(data.physicalDevice, &props2);
        d->texel = props.shadingRateTexelSize;
        d->on = d->cmdBindShadingRateImage && d->createImageView && d->cmdCopyBufferToImage &&
                props.shadingRatePaletteSize >= std::size(kPalette) && d->texel.width > 0 &&
                d->texel.height > 0;
        const Settings& s = settings();
        EVR_LOG("vrs: %s: rate texel %ux%u, palette up to %u entries; %s", d->on ? "on" : "off",
                d->texel.width, d->texel.height, props.shadingRatePaletteSize,
                s.mode == Mode::Uniform
                    ? (s.rate == foveation::kRateHalf ? "every pass at 2x2" : "every pass at 4x4")
                : s.mode == Mode::EyeTest ? "eye test: eye L's left half and eye R's right half at 4x4"
                                          : "fixed foveation around head-forward");
    }
    vrs_gui::onDeviceCreated(data, d->on);
    std::unique_lock lock(g_devicesMutex);
    g_devices[keyOf(data.device)] = d;
}

void onDeviceDestroyed(VkDevice device) {
    std::unique_lock lock(g_devicesMutex);
    g_devices.erase(keyOf(device));
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

PFN_vkVoidFunction findHook(const char* name) {
    if (!wanted()) {
        return nullptr;
    }
    if (std::strcmp(name, "vkCreateGraphicsPipelines") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&CreateGraphicsPipelines);
    }
    if (std::strcmp(name, "vkCmdBeginRenderPass") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&CmdBeginRenderPass);
    }
    return vrs_gui::findHook(name); // the image views and framebuffers, to find the GUI passes
}

} // namespace evr::vkcore::vrs_nv
