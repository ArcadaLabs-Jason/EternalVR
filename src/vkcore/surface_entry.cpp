// The layer's surface entry points (surface_entry.hpp).

#include "vkcore/surface_entry.hpp"

#include "vkcore/keep_active.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mirror_place.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/virtual_client.hpp"
#include "vkcore/xr_presenter.hpp"

#include <windows.h>

#include <cstring>

namespace evr::vkcore {

namespace {

bool isGameInstance(const InstanceData* data) {
    return data && data->isGame && !passThroughThread();
}

// The game's window: the presenter keeps the game active while VR runs, and the render size answers its
// client area.
VKAPI_ATTR VkResult VKAPI_CALL CreateWin32SurfaceKHR(VkInstance instance,
                                                     const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
                                                     const VkAllocationCallbacks* pAllocator,
                                                     VkSurfaceKHR* pSurface) {
    InstanceData* data = findInstanceData(instance);
    if (!data) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    auto next = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        data->nextGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
    if (!next) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    const bool game = isGameInstance(data);
    if (game) {
        placeGameWindow(pCreateInfo->hwnd);
    }
    const VkResult result = next(instance, pCreateInfo, pAllocator, pSurface);
    if (result == VK_SUCCESS && game) {
        EVR_LOG("vkCreateWin32SurfaceKHR: game window %p", static_cast<void*>(pCreateInfo->hwnd));
        setGameWindow(pCreateInfo->hwnd);
        virtual_client::onGameSurface(*data, pCreateInfo->hwnd, *pSurface);
        if (mp_guard::allowsGameTouch()) {
            mirror_place::bringToFrontOnce(pCreateInfo->hwnd); // after the placement, in the same order
        }
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroySurfaceKHR(VkInstance instance,
                                             VkSurfaceKHR surface,
                                             const VkAllocationCallbacks* pAllocator) {
    InstanceData* data = findInstanceData(instance);
    if (!data) {
        return;
    }
    if (isGameInstance(data)) {
        virtual_client::onSurfaceDestroyed(surface);
    }
    auto next = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
        data->nextGetInstanceProcAddr(instance, "vkDestroySurfaceKHR"));
    if (next) {
        next(instance, surface, pAllocator);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice,
                                                                       VkSurfaceKHR surface,
                                                                       VkSurfaceCapabilitiesKHR* pCaps) {
    InstanceData* data = findInstanceData(physicalDevice);
    if (!data || !data->vk.GetPhysicalDeviceSurfaceCapabilitiesKHR) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const VkResult result = data->vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, pCaps);
    if (result == VK_SUCCESS && isGameInstance(data)) {
        virtual_client::adjustCapabilities(surface, *pCaps);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice,
                                         const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo,
                                         VkSurfaceCapabilities2KHR* pCaps) {
    InstanceData* data = findInstanceData(physicalDevice);
    if (!data || !data->nextSurfaceCapabilities2) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const VkResult result = data->nextSurfaceCapabilities2(physicalDevice, pSurfaceInfo, pCaps);
    if (result == VK_SUCCESS && isGameInstance(data)) {
        virtual_client::adjustCapabilities(pSurfaceInfo->surface, pCaps->surfaceCapabilities);
    }
    return result;
}

} // namespace

PFN_vkVoidFunction findSurfaceHook(VkInstance instance, const char* name) {
    PFN_vkVoidFunction hook = nullptr;
    if (std::strcmp(name, "vkCreateWin32SurfaceKHR") == 0) {
        hook = reinterpret_cast<PFN_vkVoidFunction>(&CreateWin32SurfaceKHR);
    } else if (std::strcmp(name, "vkDestroySurfaceKHR") == 0) {
        hook = reinterpret_cast<PFN_vkVoidFunction>(&DestroySurfaceKHR);
    } else if (std::strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") == 0) {
        hook = reinterpret_cast<PFN_vkVoidFunction>(&GetPhysicalDeviceSurfaceCapabilitiesKHR);
    } else if (std::strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilities2KHR") == 0) {
        hook = reinterpret_cast<PFN_vkVoidFunction>(&GetPhysicalDeviceSurfaceCapabilities2KHR);
    } else {
        return nullptr;
    }
    InstanceData* data = instance ? findInstanceData(instance) : nullptr;
    if (!data || !data->nextGetInstanceProcAddr(instance, name)) {
        return nullptr;
    }
    return hook;
}

} // namespace evr::vkcore
