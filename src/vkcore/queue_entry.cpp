// The layer's queue entry points (queue_entry.hpp).

#include "vkcore/queue_entry.hpp"

#include "vkcore/swapchain_entry.hpp"

#include <cstdint>
#include <cstring>
#include <mutex>

namespace evr::vkcore {

namespace {

void recordQueue(DeviceData& data, VkQueue queue, std::uint32_t family) {
    std::lock_guard lock(data.queueMutex);
    data.queueFamilies[queue] = family;
}

VKAPI_ATTR void VKAPI_CALL GetDeviceQueue(VkDevice device,
                                          std::uint32_t family,
                                          std::uint32_t index,
                                          VkQueue* pQueue) {
    DeviceData* data = findDeviceData(device);
    data->vk.GetDeviceQueue(device, family, index, pQueue);
    if (*pQueue) {
        recordQueue(*data, *pQueue, family);
    }
}

VKAPI_ATTR void VKAPI_CALL GetDeviceQueue2(VkDevice device,
                                           const VkDeviceQueueInfo2* pInfo,
                                           VkQueue* pQueue) {
    DeviceData* data = findDeviceData(device);
    data->vk.GetDeviceQueue2(device, pInfo, pQueue);
    if (*pQueue) {
        recordQueue(*data, *pQueue, pInfo->queueFamilyIndex);
    }
}

} // namespace

PFN_vkVoidFunction findQueueHook(const char* name) {
    if (std::strcmp(name, "vkGetDeviceQueue") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceQueue);
    }
    if (std::strcmp(name, "vkGetDeviceQueue2") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceQueue2);
    }
    return nullptr;
}

} // namespace evr::vkcore
