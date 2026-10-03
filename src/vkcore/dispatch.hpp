#pragma once

// Per-instance and per-device state of the layer, with the next layer's entry points.

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace evr::vkcore {

class XrPresenter;

// One entry per line; clang-format would pack them.
// clang-format off
#define EVR_INSTANCE_FUNCTIONS(X)                                                                            \
    X(DestroyInstance)                                                                                       \
    X(EnumerateDeviceExtensionProperties)                                                                    \
    X(GetPhysicalDeviceProperties)                                                                           \
    X(GetPhysicalDeviceProperties2)                                                                          \
    X(GetPhysicalDeviceFeatures2)                                                                            \
    X(GetPhysicalDeviceMemoryProperties)                                                                     \
    X(GetPhysicalDeviceFormatProperties)                                                                     \
    X(GetPhysicalDeviceImageFormatProperties2)                                                               \
    X(GetPhysicalDeviceExternalSemaphoreProperties)                                                          \
    X(GetPhysicalDeviceQueueFamilyProperties)                                                                \
    X(GetPhysicalDeviceSurfacePresentModesKHR)                                                               \
    X(GetPhysicalDeviceSurfaceCapabilitiesKHR)

#define EVR_DEVICE_FUNCTIONS(X)                                                                              \
    X(DestroyDevice)                                                                                         \
    X(GetDeviceQueue)                                                                                        \
    X(GetDeviceQueue2)                                                                                       \
    X(CreateSwapchainKHR)                                                                                    \
    X(DestroySwapchainKHR)                                                                                   \
    X(GetSwapchainImagesKHR)                                                                                 \
    X(QueuePresentKHR)                                                                                       \
    X(AcquireNextImageKHR)                                                                                   \
    X(QueueSubmit)                                                                                           \
    X(DeviceWaitIdle)                                                                                        \
    X(CreateSemaphore)                                                                                       \
    X(DestroySemaphore)                                                                                      \
    X(GetSemaphoreCounterValueKHR)                                                                           \
    X(ImportSemaphoreWin32HandleKHR)                                                                         \
    X(CreateCommandPool)                                                                                     \
    X(DestroyCommandPool)                                                                                    \
    X(AllocateCommandBuffers)                                                                                \
    X(ResetCommandBuffer)                                                                                    \
    X(BeginCommandBuffer)                                                                                    \
    X(EndCommandBuffer)                                                                                      \
    X(CmdPipelineBarrier)                                                                                    \
    X(CmdCopyImage)                                                                                          \
    X(CmdBlitImage)                                                                                          \
    X(CmdClearColorImage)                                                                                    \
    X(CreateImage)                                                                                           \
    X(DestroyImage)                                                                                          \
    X(GetImageMemoryRequirements)                                                                            \
    X(AllocateMemory)                                                                                        \
    X(FreeMemory)                                                                                            \
    X(BindImageMemory)                                                                                       \
    X(CreateBuffer)                                                                                          \
    X(DestroyBuffer)                                                                                         \
    X(GetBufferMemoryRequirements)                                                                           \
    X(BindBufferMemory)                                                                                      \
    X(MapMemory)                                                                                             \
    X(UnmapMemory)                                                                                           \
    X(InvalidateMappedMemoryRanges)                                                                          \
    X(CmdCopyImageToBuffer)                                                                                  \
    X(GetMemoryWin32HandlePropertiesKHR)
// clang-format on

#define EVR_DECLARE_FN(name) PFN_vk##name name = nullptr;

struct InstanceDispatch {
    EVR_INSTANCE_FUNCTIONS(EVR_DECLARE_FN)
};

struct DeviceDispatch {
    EVR_DEVICE_FUNCTIONS(EVR_DECLARE_FN)
};

struct InstanceData {
    VkInstance instance = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr nextGetInstanceProcAddr = nullptr;
    InstanceDispatch vk;
    std::uint32_t apiVersion = VK_API_VERSION_1_0;
    // True when the application names match the game's (T-082); other instances pass through.
    bool isGame = false;
    // A surface maintenance extension is on (the layer asked for it under Route S): the game's device may
    // then get the swapchain maintenance one of the same family, with which the layer hands images back
    // without presenting them.
    bool surfaceMaintenance1 = false;
    // Which: VK_KHR_surface_maintenance1, the older VK_EXT_surface_maintenance1, or both. The device takes
    // VK_KHR_swapchain_maintenance1 when the KHR one is on and the device lists it, else the EXT one (some
    // drivers list only that on the device). The structures and values are the same.
    bool surfaceMaintenance1Khr = false;
    bool surfaceMaintenance1Ext = false;
    // The next layer's vkGetPhysicalDeviceSurfaceCapabilities2KHR (null without it): the layer's own hook
    // calls it, and the render size queries present scaling through it once VK_KHR_surface_maintenance1 is
    // on.
    PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR nextSurfaceCapabilities2 = nullptr;
};

struct DeviceData {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    InstanceData* instance = nullptr;
    PFN_vkGetDeviceProcAddr nextGetDeviceProcAddr = nullptr;
    PFN_vkSetDeviceLoaderData setDeviceLoaderData = nullptr;
    DeviceDispatch vk;

    // External memory, external semaphore and timeline semaphore support were enabled.
    bool interopEnabled = false;
    bool luidValid = false;
    std::uint8_t luid[VK_LUID_SIZE] = {};

    std::mutex queueMutex;
    std::unordered_map<VkQueue, std::uint32_t> queueFamilies;
    std::vector<VkQueueFlags> queueFamilyFlags;
    // VK_KHR_swapchain_maintenance1 is on: presents the game's window does not need are handed back.
    PFN_vkReleaseSwapchainImagesKHR releaseSwapchainImages = nullptr;

    std::unique_ptr<XrPresenter> presenter;
};

// The loader's link info of the given function in a create info's pNext chain.
template <typename Info>
Info* findLayerCreateInfo(const void* pNext, VkStructureType type, VkLayerFunction function) {
    auto* node = static_cast<const VkBaseInStructure*>(pNext);
    while (node) {
        if (node->sType == type) {
            auto* info = reinterpret_cast<Info*>(const_cast<VkBaseInStructure*>(node));
            if (info->function == function) {
                return info;
            }
        }
        node = node->pNext;
    }
    return nullptr;
}

// True when the list names the extension.
inline bool hasExtension(const std::vector<const char*>& list, const char* name) {
    return std::any_of(list.begin(), list.end(), [name](const char* e) { return std::strcmp(e, name) == 0; });
}

// A Vulkan API version as "major.minor.patch".
inline std::string versionString(std::uint32_t v) {
    return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "." +
           std::to_string(VK_API_VERSION_PATCH(v));
}

} // namespace evr::vkcore
