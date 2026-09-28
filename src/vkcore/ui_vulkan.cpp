#include "vkcore/ui_vulkan.hpp"

#include "ui_layer/layout_tracker.hpp"
#include "ui_layer/ui_settings.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/shader_dump.hpp"

#include <atomic>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace evr::vkcore::ui_vulkan {

namespace {

using DispatchKey = void*;

template <typename Handle>
DispatchKey keyOf(Handle handle) {
    return *reinterpret_cast<void**>(handle);
}

template <typename Handle>
ui_layer::LayoutTracker::Handle handleValue(Handle handle) {
    return reinterpret_cast<ui_layer::LayoutTracker::Handle>(handle);
}

struct UiDevice {
    PFN_vkCreateImage createImage = nullptr;
    PFN_vkDestroyImage destroyImage = nullptr;
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
    PFN_vkCmdExecuteCommands cmdExecuteCommands = nullptr;
    PFN_vkQueueSubmit queueSubmit = nullptr;
    bool isGame = false; // other devices (a runtime's own) pass straight through
};

// Allocated once and never destroyed (see layer_entry.cpp: no teardown at process exit).
std::shared_mutex& g_devicesMutex = *new std::shared_mutex;
auto& g_devices = *new std::unordered_map<DispatchKey, UiDevice>;
std::mutex& g_recordsMutex = *new std::mutex;
auto& g_records = *new std::unordered_map<VkImage, ui_layer::ImageRecord>;
ui_layer::LayoutTracker& g_tracker = *new ui_layer::LayoutTracker;
std::atomic<std::uint64_t> g_candidates{0};
std::atomic<std::uint64_t> g_notFollowed{0};
std::atomic<std::uint64_t> g_watchChanges{0};
// The one image whose layout is followed: the GUI target the game uses now (set from the present hook).
std::atomic<ui_layer::LayoutTracker::Handle> g_watched{0};

const UiDevice* deviceOf(DispatchKey key) {
    std::shared_lock lock(g_devicesMutex);
    const auto it = g_devices.find(key);
    return it == g_devices.end() ? nullptr : &it->second;
}

bool readEnabled() {
    std::vector<std::string> warnings;
    const ui_layer::UiSettings s = ui_layer::readUiSettings(
        [](std::wstring_view name) -> std::optional<std::wstring> {
            std::wstring value;
            if (!readEnv(std::wstring(name).c_str(), value)) {
                return std::nullopt;
            }
            return value;
        },
        warnings);
    return s.enabled;
}

// ---- Hooks ---------------------------------------------------------------------------------------------

VKAPI_ATTR VkResult VKAPI_CALL CreateImage(VkDevice device,
                                           const VkImageCreateInfo* pCreateInfo,
                                           const VkAllocationCallbacks* pAllocator,
                                           VkImage* pImage) {
    const UiDevice* d = deviceOf(keyOf(device));
    ui_layer::ImageCreateDesc desc;
    desc.imageType = pCreateInfo->imageType;
    desc.format = pCreateInfo->format;
    desc.width = pCreateInfo->extent.width;
    desc.height = pCreateInfo->extent.height;
    desc.depth = pCreateInfo->extent.depth;
    desc.mipLevels = pCreateInfo->mipLevels;
    desc.arrayLayers = pCreateInfo->arrayLayers;
    desc.samples = pCreateInfo->samples;
    desc.usage = pCreateInfo->usage;
    const std::optional<std::uint32_t> usage =
        d->isGame && mp_guard::allowsGameTouch() ? ui_layer::candidateUsage(desc) : std::nullopt;
    if (!usage) {
        return d->createImage(device, pCreateInfo, pAllocator, pImage);
    }
    VkImageCreateInfo info = *pCreateInfo;
    info.usage = *usage;
    const VkResult result = d->createImage(device, &info, pAllocator, pImage);
    if (result != VK_SUCCESS) {
        return result;
    }
    ui_layer::ImageRecord record;
    record.width = info.extent.width;
    record.height = info.extent.height;
    record.usage = info.usage;
    record.sharingMode = info.sharingMode;
    if (info.sharingMode == VK_SHARING_MODE_CONCURRENT && info.pQueueFamilyIndices) {
        record.queueFamilies.assign(info.pQueueFamilyIndices,
                                    info.pQueueFamilyIndices + info.queueFamilyIndexCount);
    }
    {
        std::lock_guard lock(g_recordsMutex);
        g_records[*pImage] = record;
    }
    const std::uint64_t n = ++g_candidates;
    if (n <= 8) {
        EVR_LOG("ui: image %p (%ux%u RGBA8 render target, sharing %s over %u famil%s) prepared for copying",
                reinterpret_cast<void*>(*pImage), info.extent.width, info.extent.height,
                info.sharingMode == VK_SHARING_MODE_CONCURRENT ? "concurrent" : "exclusive",
                info.queueFamilyIndexCount, info.queueFamilyIndexCount == 1 ? "y" : "ies");
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyImage(VkDevice device,
                                        VkImage image,
                                        const VkAllocationCallbacks* pAllocator) {
    const UiDevice* d = deviceOf(keyOf(device));
    if (image != VK_NULL_HANDLE) {
        bool known = false;
        {
            std::lock_guard lock(g_recordsMutex);
            known = g_records.erase(image) != 0;
        }
        if (known) {
            g_tracker.removeCandidate(handleValue(image));
            auto expected = handleValue(image);
            g_watched.compare_exchange_strong(expected, 0);
        }
    }
    d->destroyImage(device, image, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL CmdPipelineBarrier(VkCommandBuffer commandBuffer,
                                              VkPipelineStageFlags srcStageMask,
                                              VkPipelineStageFlags dstStageMask,
                                              VkDependencyFlags dependencyFlags,
                                              std::uint32_t memoryBarrierCount,
                                              const VkMemoryBarrier* pMemoryBarriers,
                                              std::uint32_t bufferMemoryBarrierCount,
                                              const VkBufferMemoryBarrier* pBufferMemoryBarriers,
                                              std::uint32_t imageMemoryBarrierCount,
                                              const VkImageMemoryBarrier* pImageMemoryBarriers) {
    for (std::uint32_t i = 0; i < imageMemoryBarrierCount; ++i) {
        g_tracker.onBarrier(handleValue(commandBuffer), handleValue(pImageMemoryBarriers[i].image),
                            pImageMemoryBarriers[i].newLayout);
    }
    deviceOf(keyOf(commandBuffer))
        ->cmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount,
                             pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers,
                             imageMemoryBarrierCount, pImageMemoryBarriers);
}

VKAPI_ATTR VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer commandBuffer,
                                                  const VkCommandBufferBeginInfo* pBeginInfo) {
    g_tracker.onBegin(handleValue(commandBuffer));
    return deviceOf(keyOf(commandBuffer))->beginCommandBuffer(commandBuffer, pBeginInfo);
}

VKAPI_ATTR void VKAPI_CALL CmdExecuteCommands(VkCommandBuffer commandBuffer,
                                              std::uint32_t commandBufferCount,
                                              const VkCommandBuffer* pCommandBuffers) {
    std::vector<ui_layer::LayoutTracker::Handle> secondaries(commandBufferCount);
    for (std::uint32_t i = 0; i < commandBufferCount; ++i) {
        secondaries[i] = handleValue(pCommandBuffers[i]);
    }
    g_tracker.onExecute(handleValue(commandBuffer), secondaries.data(), secondaries.size());
    deviceOf(keyOf(commandBuffer))->cmdExecuteCommands(commandBuffer, commandBufferCount, pCommandBuffers);
}

VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit(VkQueue queue,
                                           std::uint32_t submitCount,
                                           const VkSubmitInfo* pSubmits,
                                           VkFence fence) {
    const UiDevice* d = deviceOf(keyOf(queue));
    const VkResult result = d->queueSubmit(queue, submitCount, pSubmits, fence);
    if (result == VK_SUCCESS) {
        std::vector<ui_layer::LayoutTracker::Handle> cbs;
        for (std::uint32_t s = 0; s < submitCount; ++s) {
            for (std::uint32_t i = 0; i < pSubmits[s].commandBufferCount; ++i) {
                cbs.push_back(handleValue(pSubmits[s].pCommandBuffers[i]));
            }
        }
        g_tracker.onSubmit(handleValue(queue), cbs.data(), cbs.size());
    }
    return result;
}

} // namespace

bool enabled() {
    static const bool on = readEnabled();
    return on;
}

void onDeviceCreated(VkDevice device, PFN_vkGetDeviceProcAddr nextGetDeviceProcAddr, bool isGame) {
    if (!enabled()) {
        return;
    }
    UiDevice d;
    d.isGame = isGame;
    d.createImage = reinterpret_cast<PFN_vkCreateImage>(nextGetDeviceProcAddr(device, "vkCreateImage"));
    d.destroyImage = reinterpret_cast<PFN_vkDestroyImage>(nextGetDeviceProcAddr(device, "vkDestroyImage"));
    d.cmdPipelineBarrier =
        reinterpret_cast<PFN_vkCmdPipelineBarrier>(nextGetDeviceProcAddr(device, "vkCmdPipelineBarrier"));
    // The shader dump hooks these two as well: with both on, ours calls the dump's, which calls the next.
    const auto chained = [&](const char* name) {
        const PFN_vkVoidFunction dump = shader_dump::findHook(name);
        return dump ? dump : nextGetDeviceProcAddr(device, name);
    };
    d.beginCommandBuffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(chained("vkBeginCommandBuffer"));
    d.cmdExecuteCommands = reinterpret_cast<PFN_vkCmdExecuteCommands>(chained("vkCmdExecuteCommands"));
    d.queueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(nextGetDeviceProcAddr(device, "vkQueueSubmit"));
    if (!d.createImage || !d.destroyImage || !d.cmdPipelineBarrier || !d.beginCommandBuffer ||
        !d.cmdExecuteCommands || !d.queueSubmit) {
        // Every hook needs its next function; a device without one would crash in them.
        EVR_LOG("ui: a device function is missing; this device is not changed or followed");
        d.isGame = false;
    }
    {
        std::unique_lock lock(g_devicesMutex);
        g_devices[keyOf(device)] = d;
    }
    if (d.isGame) {
        EVR_LOG("ui: UI layer on: images created like the GUI target get TRANSFER_SRC, their layouts are "
                "followed");
    }
}

void onDeviceDestroyed(VkDevice device) {
    std::unique_lock lock(g_devicesMutex);
    g_devices.erase(keyOf(device));
}

PFN_vkVoidFunction findHook(const char* name) {
    if (!enabled()) {
        return nullptr;
    }
#define EVR_UI_HOOK(fn)                                                                                      \
    if (std::strcmp(name, "vk" #fn) == 0) {                                                                  \
        return reinterpret_cast<PFN_vkVoidFunction>(&fn);                                                    \
    }
    EVR_UI_HOOK(CreateImage)
    EVR_UI_HOOK(DestroyImage)
    EVR_UI_HOOK(CmdPipelineBarrier)
    EVR_UI_HOOK(BeginCommandBuffer)
    EVR_UI_HOOK(CmdExecuteCommands)
    EVR_UI_HOOK(QueueSubmit)
#undef EVR_UI_HOOK
    return nullptr;
}

std::optional<ui_layer::ImageRecord> recordOf(VkImage image) {
    std::lock_guard lock(g_recordsMutex);
    const auto it = g_records.find(image);
    if (it == g_records.end()) {
        return std::nullopt;
    }
    return it->second;
}

void watch(VkImage image) {
    const auto value = handleValue(image);
    if (g_watched.load() == value) {
        return;
    }
    const auto previous = g_watched.exchange(value);
    if (previous != 0) {
        g_tracker.removeCandidate(previous);
    }
    if (!g_tracker.addCandidate(value)) {
        ++g_notFollowed;
    }
    if (++g_watchChanges <= 8) {
        EVR_LOG("ui: GUI target is image %p; its layout is followed from here",
                reinterpret_cast<void*>(image));
    }
}

std::optional<ImageState> stateOf(VkImage image) {
    const auto s = g_tracker.stateOf(handleValue(image));
    if (!s) {
        return std::nullopt;
    }
    return ImageState{static_cast<VkImageLayout>(s->layout), reinterpret_cast<VkQueue>(s->queue)};
}

Counters counters() {
    return Counters{g_candidates.load(), g_notFollowed.load(), g_watchChanges.load()};
}

} // namespace evr::vkcore::ui_vulkan
