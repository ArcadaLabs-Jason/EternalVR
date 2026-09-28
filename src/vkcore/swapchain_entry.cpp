// The game's swapchain and its presents (swapchain_entry.hpp).

#include "vkcore/swapchain_entry.hpp"

#include "vkcore/cpu_timing.hpp"
#include "vkcore/gpu_timing.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/shader_dump.hpp"
#include "vkcore/stereo_present.hpp"
#include "vkcore/virtual_client.hpp"
#include "vkcore/window_timing.hpp"
#include "vkcore/xr_presenter.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>

namespace evr::vkcore {

namespace {

// Acquires and presents of the game that did not succeed: the first few logged, every 1000th after, and
// counted (a swapchain the game keeps recreating, or a present the driver refuses, shows here).
std::atomic<std::uint64_t> g_failedResults{0};
void logResult(const char* call, VkResult result) {
    if (result == VK_SUCCESS) {
        return;
    }
    const std::uint64_t n = g_failedResults.fetch_add(1, std::memory_order_relaxed);
    if (n < 16 || n % 1000 == 0) {
        EVR_LOG("swapchain: %s returned %d (%llu such result(s) so far)", call, result,
                static_cast<unsigned long long>(n + 1));
    }
}

VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device,
                                                  const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkSwapchainKHR* pSwapchain) {
    DeviceData* data = findDeviceData(device);
    VkSwapchainCreateInfoKHR info = *pCreateInfo;
    info.presentMode = stereoPresentMode(*data, *pCreateInfo);
    if (info.presentMode != pCreateInfo->presentMode) {
        EVR_LOG("stereo: present mode %d instead of the game's %d (two presents per tick)", info.presentMode,
                pCreateInfo->presentMode);
    }
    info.minImageCount = stereoImageCount(*data, *pCreateInfo);
    if (info.minImageCount != pCreateInfo->minImageCount) {
        EVR_LOG("stereo: %u swapchain image(s) instead of the game's %u (no present waits for the display)",
                info.minImageCount, pCreateInfo->minImageCount);
    }
    VkSwapchainPresentScalingCreateInfoKHR scaling{};
    const bool scaled = virtual_client::addPresentScaling(info, scaling);
    pCreateInfo = &info;
    const VkResult result = data->vk.CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    virtual_client::onSwapchainCreated(result, result == VK_SUCCESS ? *pSwapchain : VK_NULL_HANDLE, scaled);
    EVR_LOG("vkCreateSwapchainKHR %ux%u format %d colour space %d, %u min image(s), usage 0x%x, present mode "
            "%d: %d",
            pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height, pCreateInfo->imageFormat,
            pCreateInfo->imageColorSpace, pCreateInfo->minImageCount, pCreateInfo->imageUsage,
            pCreateInfo->presentMode, result);
    if (scaled) {
        EVR_LOG(
            "size: the swapchain is scaled into the window's real client area (scaling 0x%x, gravity 0x%x)",
            scaling.scalingBehavior, scaling.presentGravityX);
    }
    if (result != VK_SUCCESS || !data->interopEnabled) {
        return result;
    }
    if (!data->presenter) {
        data->presenter = std::make_unique<XrPresenter>(*data);
    }
    data->presenter->onSwapchainCreated(*pSwapchain, *pCreateInfo);
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice device,
                                               VkSwapchainKHR swapchain,
                                               const VkAllocationCallbacks* pAllocator) {
    DeviceData* data = findDeviceData(device);
    if (data->presenter) {
        data->presenter->onSwapchainDestroyed(swapchain);
    }
    virtual_client::onSwapchainDestroyed(swapchain);
    data->vk.DestroySwapchainKHR(device, swapchain, pAllocator);
}

// Timed only: an acquire that waits for the desktop display shows in the window line (window_timing.hpp).
VKAPI_ATTR VkResult VKAPI_CALL AcquireNextImageKHR(VkDevice device,
                                                   VkSwapchainKHR swapchain,
                                                   std::uint64_t timeout,
                                                   VkSemaphore semaphore,
                                                   VkFence fence,
                                                   std::uint32_t* pImageIndex) {
    DeviceData* data = findDeviceData(device);
    const std::uint64_t start = window_timing::nowMicros();
    const VkResult result =
        data->vk.AcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
    if (data->presenter) {
        window_timing::addAcquire(window_timing::nowMicros() - start);
    }
    logResult("vkAcquireNextImageKHR", result);
    return virtual_client::scaledResult(swapchain, result);
}

// The present through the XR presenter when there is one.
VkResult presentOnce(DeviceData& data, VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    if (!data.presenter) {
        return data.vk.QueuePresentKHR(queue, pPresentInfo);
    }
    std::uint32_t family = 0;
    bool known = false;
    {
        std::lock_guard lock(data.queueMutex);
        const auto it = data.queueFamilies.find(queue);
        if (it != data.queueFamilies.end()) {
            family = it->second;
            known = true;
        }
    }
    if (!known) {
        return data.vk.QueuePresentKHR(queue, pPresentInfo);
    }
    return data.presenter->present(queue, family, pPresentInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    mp_guard::poll();
    virtual_client::poll();
    DeviceData* data = findDeviceData(queue);
    shader_dump::onPresent(data->device);
    gpu_timing::onPresent(queue); // closes the frame (ETERNALVR_GPU_TIMING)
    VkResult result = [&] {
        cpu_timing::Scope timed(cpu_timing::Stage::Present); // ETERNALVR_CPU_TIMING
        return presentOnce(*data, queue, pPresentInfo);
    }();
    logResult("vkQueuePresentKHR", result);
    virtual_client::scaledResults(*pPresentInfo, result);
    return result;
}

} // namespace

PFN_vkVoidFunction findSwapchainHook(const char* name) {
#define EVR_HOOK(fn)                                                                                         \
    if (std::strcmp(name, "vk" #fn) == 0) {                                                                  \
        return reinterpret_cast<PFN_vkVoidFunction>(&fn);                                                    \
    }
    EVR_HOOK(CreateSwapchainKHR)
    EVR_HOOK(DestroySwapchainKHR)
    EVR_HOOK(QueuePresentKHR)
    EVR_HOOK(AcquireNextImageKHR)
#undef EVR_HOOK
    return nullptr;
}

} // namespace evr::vkcore
