#pragma once

// Internal to the layer's loader entry points, shared by layer_entry.cpp (the instance, the proc address
// lookups, the loader's negotiation) and device_entry.cpp (vkCreateDevice, vkDestroyDevice, the device hooks'
// lookup): the dispatch maps and the device entry points the lookups hand out.

#include "vkcore/dispatch.hpp"

#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace evr::vkcore::layer_entry_detail {

// ---------------------------------------------------------------------------------------------------
// Dispatch maps, keyed by the loader's dispatch key (the first pointer inside a dispatchable handle).

using DispatchKey = void*;

template <typename Handle>
DispatchKey keyOf(Handle handle) {
    return *reinterpret_cast<void**>(handle);
}

// The maps and the pass-through flag (layer_entry.cpp).
extern std::shared_mutex& g_mapMutex;
extern std::unordered_map<DispatchKey, std::unique_ptr<InstanceData>>& g_instances;
extern std::unordered_map<DispatchKey, std::unique_ptr<DeviceData>>& g_devices;
extern thread_local bool t_passThrough;

template <typename Handle>
InstanceData* findInstance(Handle handle) {
    std::shared_lock lock(g_mapMutex);
    const auto it = g_instances.find(keyOf(handle));
    return it == g_instances.end() ? nullptr : it->second.get();
}

template <typename Handle>
DeviceData* findDevice(Handle handle) {
    std::shared_lock lock(g_mapMutex);
    const auto it = g_devices.find(keyOf(handle));
    return it == g_devices.end() ? nullptr : it->second.get();
}

// device_entry.cpp: vkCreateDevice, and the lookup of the device hooks (`device` null: an instance lookup).
VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physicalDevice,
                                            const VkDeviceCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkDevice* pDevice);
PFN_vkVoidFunction findDeviceHook(VkDevice device, const char* name);
// The layer's own hooks of the device's life (vkDestroyDevice, the queues, the swapchain and its presents),
// matched by name only: they never depend on a feature's settings, so a lookup whose feature part throws
// still hands them out (the presenter's and the device's records then always see their destroys).
PFN_vkVoidFunction findLifecycleHook(const char* name) noexcept;

} // namespace evr::vkcore::layer_entry_detail
