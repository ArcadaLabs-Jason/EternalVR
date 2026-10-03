// Foveated rendering through VK_NV_shading_rate_image (vrs_nv.hpp): the device, the pipelines' palette and
// the rate image bound before each render pass.

#include "vkcore/vrs_nv.hpp"

#include "features/foveation/eye_targets.hpp"
#include "features/foveation/rate_pattern.hpp"
#include "vkcore/log.hpp"
#include "vkcore/ui_vulkan.hpp"
#include "vkcore/vrs_gui.hpp"
#include "vkcore/vrs_nv_impl.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace evr::vkcore::vrs_nv {

namespace {

// Palette entries: a rate image texel's value picks one (foveation::kRateFull, kRateHalf, kRateQuarter).
constexpr VkShadingRatePaletteEntryNV kPalette[] = {
    VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_PIXEL_NV,
    VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X2_PIXELS_NV,
    VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X4_PIXELS_NV,
};

// Render passes smaller than this (in either direction) keep full rate: shadow cascades, small effects.
constexpr std::uint32_t kMinTarget = 256;

// Allocated once and never destroyed (see layer_entry.cpp: no teardown at process exit).
std::shared_mutex& g_devicesMutex = *new std::shared_mutex;
auto& g_devices = *new std::unordered_map<DispatchKey, VrsDevice*>;

// Some device's render passes take the eye of their frame (VrsDevice::followsFrames): the render-view job's
// counter is noted for them.
std::atomic<bool> g_followsFrames{false};

VkPhysicalDeviceShadingRateImageFeaturesNV g_feature{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_FEATURES_NV};

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
        // The eye of the backend frame these commands belong to (vrs_pass_eye.cpp): the render thread records
        // a frame while the game's chain already runs the next one, so the chain's own eye (seqRenderEye) is
        // the other eye. Mono frames (the cinema screen, menus), eye frames without their view, untagged
        // frames (tags out of step) and passes whose frame is not known keep full rate unless every eye gets
        // the same image.
        const int eye = settings().mode == Mode::Uniform ? 0 : passEye(*d, commandBuffer);
        VkImageView view = VK_NULL_HANDLE;
        if (eye != kNoEye && extent.width >= kMinTarget && extent.height >= kMinTarget &&
            pRenderPassBegin->renderArea.offset.x == 0 && pRenderPassBegin->renderArea.offset.y == 0) {
            // Passes into the GUI target (the game's menus and HUD) keep full rate: their text turns coarse.
            if (!vrs_gui::drawsGuiTarget(commandBuffer, *pRenderPassBegin)) {
                std::uint64_t size = d->eyeSize.load(std::memory_order_relaxed);
                if (size == 0) {
                    size = d->swapchainSize.load(std::memory_order_relaxed);
                }
                const foveation::TargetSize eyeImage{static_cast<std::uint32_t>(size >> 32),
                                                     static_cast<std::uint32_t>(size)};
                if (settings().mode == Mode::Uniform ||
                    foveation::isEyeSpaceTarget({extent.width, extent.height}, eyeImage)) {
                    view = viewFor(*d, commandBuffer, extent, eye);
                } else if (eyeImage.width != 0) {
                    noteOtherTarget(*d, extent, eyeImage);
                }
            } else {
                d->eyeSize.store(packed(extent), std::memory_order_relaxed);
                if (d->guiPasses.fetch_add(1) == 0) {
                    EVR_LOG("vrs: first render pass into the GUI target (image %p, %ux%u) kept at full rate; "
                            "the game's menus and HUD are not foveated",
                            reinterpret_cast<void*>(ui_vulkan::guiTarget()), extent.width, extent.height);
                }
            }
        }
        d->cmdBindShadingRateImage(commandBuffer, view, VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV);
        d->binds[static_cast<std::size_t>(eye)].fetch_add(1);
        if (view) {
            d->coarse[static_cast<std::size_t>(eye)].fetch_add(1, std::memory_order_relaxed);
        }
        const std::uint64_t full = view ? d->fullRate.load() : d->fullRate.fetch_add(1) + 1;
        if (d->passes.fetch_add(1) % 200000 == 199999) {
            EVR_LOG("vrs: render passes: %llu recorded for eye L, %llu for eye R, %llu mono, untagged or not "
                    "known; %llu of them at full rate (%llu into the GUI target); %llu pipeline(s) with the "
                    "palette",
                    static_cast<unsigned long long>(d->binds[0].load()),
                    static_cast<unsigned long long>(d->binds[1].load()),
                    static_cast<unsigned long long>(d->binds[2].load()),
                    static_cast<unsigned long long>(full),
                    static_cast<unsigned long long>(d->guiPasses.load()),
                    static_cast<unsigned long long>(d->pipelines.load()));
            if (settings().mode != Mode::Uniform) {
                logPassFrames(*d);
            }
        }
    }
    d->cmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
}

} // namespace

VrsDevice* deviceOf(DispatchKey key) {
    std::shared_lock lock(g_devicesMutex);
    const auto it = g_devices.find(key);
    return it == g_devices.end() ? nullptr : it->second;
}

bool wanted() {
    return settings().mode != Mode::Off;
}

bool passesFollowFrames() {
    return g_followsFrames.load(std::memory_order_relaxed);
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
    loadCommandBufferFunctions(*d);
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
        d->followsFrames = d->on && s.mode != Mode::Uniform;
        d->usesGuesses = d->followsFrames && s.guesses;
        if (d->followsFrames) {
            g_followsFrames.store(true, std::memory_order_relaxed);
        }
        EVR_LOG("vrs: %s: rate texel %ux%u, palette up to %u entries; %s", d->on ? "on" : "off",
                d->texel.width, d->texel.height, props.shadingRatePaletteSize,
                s.mode == Mode::Uniform
                    ? (s.rate == foveation::kRateHalf ? "every pass at 2x2" : "every pass at 4x4")
                : s.mode == Mode::EyeTest ? "eye test: eye L's left half and eye R's right half at 4x4"
                                          : "fixed foveation around head-forward");
        if (s.mode == Mode::Fovea) {
            EVR_LOG(
                "vrs: foveation: full rate within the region of %.0f deg, half rate within that of %.0f deg, "
                "quarter outside (each region the area of that cone, reaching the same fraction of the way "
                "to every edge of the eye's image)",
                s.fullDegrees, s.halfDegrees);
        }
        if (d->usesGuesses) {
            EVR_LOG("vrs: ETERNALVR_TEST_VRS_PARITY=1: render passes whose counters do not agree take the "
                    "frame their command buffer's recording or parity guesses (can be the other eye's)");
        }
        if (s.marks) {
            EVR_LOG("vrs: ETERNALVR_VRS_TINT=1: the headset's eye images get a dot every 32 pixels where the "
                    "eye's rate image is at half rate (yellow) or quarter rate (red), while that eye's "
                    "render passes get it");
        }
    }
    vrs_gui::onDeviceCreated(data, d->on);
    std::unique_lock lock(g_devicesMutex);
    g_devices[keyOf(data.device)] = d;
}

void onDeviceDestroyed(VkDevice device) {
    std::unique_lock lock(g_devicesMutex);
    g_devices.erase(keyOf(device));
}

void noteSwapchain(VkDevice device, VkExtent2D extent) {
    VrsDevice* d = wanted() ? deviceOf(keyOf(device)) : nullptr;
    if (!d || !d->on) {
        return;
    }
    d->swapchainSize.store(packed(extent), std::memory_order_relaxed);
    EVR_LOG(
        "vrs: the game's swapchain is %ux%u: the eye image's size until a pass into the GUI target gives it",
        extent.width, extent.height);
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
    if (const PFN_vkVoidFunction hook = findCommandBufferHook(name)) {
        return hook; // where each recording starts and ends, for the passes' frames
    }
    return vrs_gui::findHook(name); // the image views and framebuffers, to find the GUI passes
}

} // namespace evr::vkcore::vrs_nv
