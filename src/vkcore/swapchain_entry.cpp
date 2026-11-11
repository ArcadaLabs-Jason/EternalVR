// The game's swapchain and its presents (swapchain_entry.hpp).

#include "vkcore/swapchain_entry.hpp"

#include "vkcore/cpu_timing.hpp"
#include "vkcore/frame_pacing.hpp"
#include "vkcore/game_settings.hpp"
#include "vkcore/gpu_timing.hpp"
#include "vkcore/hook_guard.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/present_latency.hpp"
#include "vkcore/shader_dump.hpp"
#include "vkcore/stall_watch.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/stereo_present.hpp"
#include "vkcore/view_snapshot.hpp"
#include "vkcore/virtual_client.hpp"
#include "vkcore/vrs_nv.hpp"
#include "vkcore/window_timing.hpp"
#include "vkcore/xr_presenter.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

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

// The layer's work around the game's calls catches what it throws (hook_guard.hpp): the game's call still
// goes down once, with its own arguments where the layer's changes to them failed, and only the layer's part
// is skipped.
HookGuard g_createGuard("vkCreateSwapchainKHR (the layer's changes to the create info)");
HookGuard g_clientNote("vkCreateSwapchainKHR (the render size's note of the new swapchain)");
HookGuard g_foveationNote("vkCreateSwapchainKHR (foveation's note of the eye image size)");
HookGuard g_snapshotNote("vkCreateSwapchainKHR (the eye snapshots' note of the swapchain images)");
HookGuard g_presenterGuard("vkCreateSwapchainKHR (the presenter)");
HookGuard g_destroyGuard("vkDestroySwapchainKHR");
HookGuard g_acquireGuard("vkAcquireNextImageKHR");
HookGuard g_beforePresentGuard("vkQueuePresentKHR (the layer's work before the present)");
HookGuard g_afterPresentGuard("vkQueuePresentKHR (the layer's work after the present)");

VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device,
                                                  const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkSwapchainKHR* pSwapchain) {
    DeviceData* data = findDeviceData(device);
    VkSwapchainCreateInfoKHR info = *pCreateInfo;
    VkSwapchainPresentScalingCreateInfoKHR scaling{};
    bool scaled = false;
    if (!g_createGuard.run([&] {
            info.presentMode = stereoPresentMode(*data, *pCreateInfo);
            if (info.presentMode != pCreateInfo->presentMode) {
                EVR_LOG("stereo: present mode %d instead of the game's %d (two presents per tick)",
                        info.presentMode, pCreateInfo->presentMode);
            }
            info.minImageCount = stereoImageCount(*data, *pCreateInfo);
            if (info.minImageCount != pCreateInfo->minImageCount) {
                EVR_LOG("stereo: %u swapchain image(s) instead of the game's %u (no present waits for the "
                        "display)",
                        info.minImageCount, pCreateInfo->minImageCount);
            }
            scaled = virtual_client::addPresentScaling(info, scaling);
        })) {
        info = *pCreateInfo; // the game's own create info
        scaled = false;
    }
    pCreateInfo = &info;
    const VkResult result = data->vk.CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    // The game keeps the swapchain it got when a note or the presenter's part throws: without the presenter's
    // record its presents go to the window, not to the headset.
    g_clientNote.run([&] {
        virtual_client::onSwapchainCreated(result, result == VK_SUCCESS ? *pSwapchain : VK_NULL_HANDLE,
                                           info.surface, scaled);
    });
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
    if (result == VK_SUCCESS) {
        g_foveationNote.run([&] {
            vrs_nv::noteSwapchain(device, pCreateInfo->imageExtent); // foveation's eye image size
        });
    }
    if (result == VK_SUCCESS && view_snapshot::enabled()) {
        g_snapshotNote.run([&] {
            std::uint32_t count = 0;
            data->vk.GetSwapchainImagesKHR(device, *pSwapchain, &count, nullptr);
            std::vector<VkImage> images(count);
            data->vk.GetSwapchainImagesKHR(device, *pSwapchain, &count, images.data());
            // Parallel Eye Rendering's eye snapshots: view 0's copies are of these images.
            view_snapshot::noteSwapchain(images.data(), count, pCreateInfo->imageFormat,
                                         pCreateInfo->imageExtent);
        });
    }
    if (result != VK_SUCCESS || !data->interopEnabled) {
        return result;
    }
    g_presenterGuard.run([&] {
        if (!data->presenter) {
            data->presenter = std::make_unique<XrPresenter>(*data);
        }
        data->presenter->onSwapchainCreated(*pSwapchain, *pCreateInfo);
    });
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice device,
                                               VkSwapchainKHR swapchain,
                                               const VkAllocationCallbacks* pAllocator) {
    DeviceData* data = findDeviceData(device);
    g_destroyGuard.run([&] {
        if (data->presenter) {
            data->presenter->onSwapchainDestroyed(swapchain);
        }
        virtual_client::onSwapchainDestroyed(swapchain);
    });
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
    VkResult result = data->vk.AcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
    if (data->presenter) {
        window_timing::addAcquire(window_timing::nowMicros() - start);
    }
    logResult("vkAcquireNextImageKHR", result);
    // The render size's setting is read on its first use; without it the result is the driver's.
    g_acquireGuard.run([&] { result = virtual_client::scaledResult(swapchain, result); });
    return result;
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

// The layer's work before and after the present is skipped for this frame when it throws (a log line, a
// thread or a list it allocates); the game's present goes down once either way. The presenter catches what
// its own part of the present throws (presenter_copy.cpp).
VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    DeviceData* data = findDeviceData(queue);
    // First and on its own: the multiplayer guard's menu poll never throws (one that fails trips the guard),
    // so nothing else here can keep it from running.
    mp_guard::poll();
    std::uint64_t entered = 0;
    g_beforePresentGuard.run([&] {
        // Always on for the game's device: the gap since its last present, and our own time in this hook.
        entered = data->presenter ? stall_watch::presentEntered() : 0;
        game_settings::poll(); // the game's video settings in the log, after each map load
        virtual_client::poll();
        shader_dump::onPresent(data->device);
        gpu_timing::onPresent(queue); // closes the frame (ETERNALVR_GPU_TIMING)
        status::flush();              // a lost status write, also when VR is off
    });
    VkResult result = [&] {
        cpu_timing::Scope timed(cpu_timing::Stage::Present); // ETERNALVR_CPU_TIMING
        return presentOnce(*data, queue, pPresentInfo);
    }();
    logResult("vkQueuePresentKHR", result);
    g_afterPresentGuard.run([&] {
        virtual_client::scaledResults(*pPresentInfo, result);
        if (entered != 0) {
            stall_watch::presentLeft(entered);
        }
        // ETERNALVR_PACE=headset: a present that handed an image to the headset waits here for the headset's
        // next frame, after the hook's own timing and with no lock held (frame_pacing.hpp).
        // With CPU timing and the presenter: the game's release after the hold (present_latency.hpp).
        const bool held = frame_pacing::afterPresent();
        if (data->presenter) {
            present_latency::afterPresent(held);
        }
    });
    return result;
}

} // namespace

PFN_vkVoidFunction findSwapchainHook(const char* name) {
#define EVR_HOOK(fn)                                                                                         \
    if (std::strcmp(name, "vk" #fn) == 0) {                                                                  \
        return reinterpret_cast<PFN_vkVoidFunction>(&(fn));                                                  \
    }
    EVR_HOOK(CreateSwapchainKHR)
    EVR_HOOK(DestroySwapchainKHR)
    EVR_HOOK(QueuePresentKHR)
    EVR_HOOK(AcquireNextImageKHR)
#undef EVR_HOOK
    return nullptr;
}

} // namespace evr::vkcore
