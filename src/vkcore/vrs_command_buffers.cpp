// Where each command buffer's recording starts and ends, for the render passes' frames (vrs_pass_eye.cpp,
// stereo_seq/pass_frames.hpp): vkBeginCommandBuffer starts one, vkResetCommandBuffer and vkResetCommandPool
// end it, vkFreeCommandBuffers and vkDestroyCommandPool forget the buffers (vkAllocateCommandBuffers notes
// each buffer's pool). Hooked only while foveated rendering or a VRS experiment is on; on a device without
// the extension, and with one rate for every pass, they only call the next layer.

#include "vkcore/vrs_nv_impl.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace evr::vkcore::vrs_nv {

namespace {

template <typename Handle>
std::uint64_t handleOf(Handle handle) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
}

VKAPI_ATTR VkResult VKAPI_CALL AllocateCommandBuffers(VkDevice device,
                                                      const VkCommandBufferAllocateInfo* pAllocateInfo,
                                                      VkCommandBuffer* pCommandBuffers) {
    VrsDevice* d = deviceOf(keyOf(device));
    const VkResult result = d->allocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
    if (result == VK_SUCCESS && d->followsFrames) {
        std::lock_guard lock(d->framesMutex);
        std::vector<std::uint64_t>& buffers = d->poolBuffers[handleOf(pAllocateInfo->commandPool)];
        for (std::uint32_t i = 0; i < pAllocateInfo->commandBufferCount; ++i) {
            d->frames.forget(handleOf(pCommandBuffers[i])); // a freed buffer's handle, if its free was missed
            buffers.push_back(handleOf(pCommandBuffers[i]));
        }
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL FreeCommandBuffers(VkDevice device,
                                              VkCommandPool commandPool,
                                              std::uint32_t commandBufferCount,
                                              const VkCommandBuffer* pCommandBuffers) {
    VrsDevice* d = deviceOf(keyOf(device));
    if (d->followsFrames) {
        std::lock_guard lock(d->framesMutex);
        const auto pool = d->poolBuffers.find(handleOf(commandPool));
        for (std::uint32_t i = 0; i < commandBufferCount; ++i) {
            const std::uint64_t buffer = handleOf(pCommandBuffers[i]);
            d->frames.forget(buffer);
            if (pool != d->poolBuffers.end()) {
                std::vector<std::uint64_t>& buffers = pool->second;
                buffers.erase(std::remove(buffers.begin(), buffers.end(), buffer), buffers.end());
            }
        }
    }
    d->freeCommandBuffers(device, commandPool, commandBufferCount, pCommandBuffers);
}

VKAPI_ATTR void VKAPI_CALL DestroyCommandPool(VkDevice device,
                                              VkCommandPool commandPool,
                                              const VkAllocationCallbacks* pAllocator) {
    VrsDevice* d = deviceOf(keyOf(device));
    if (d->followsFrames) {
        std::lock_guard lock(d->framesMutex);
        if (const auto pool = d->poolBuffers.find(handleOf(commandPool)); pool != d->poolBuffers.end()) {
            for (const std::uint64_t buffer : pool->second) {
                d->frames.forget(buffer);
            }
            d->poolBuffers.erase(pool);
        }
    }
    d->destroyCommandPool(device, commandPool, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL ResetCommandPool(VkDevice device,
                                                VkCommandPool commandPool,
                                                VkCommandPoolResetFlags flags) {
    VrsDevice* d = deviceOf(keyOf(device));
    if (d->followsFrames) {
        std::lock_guard lock(d->framesMutex);
        if (const auto pool = d->poolBuffers.find(handleOf(commandPool)); pool != d->poolBuffers.end()) {
            for (const std::uint64_t buffer : pool->second) {
                d->frames.reset(buffer);
            }
        }
    }
    return d->resetCommandPool(device, commandPool, flags);
}

VKAPI_ATTR VkResult VKAPI_CALL ResetCommandBuffer(VkCommandBuffer commandBuffer,
                                                  VkCommandBufferResetFlags flags) {
    VrsDevice* d = deviceOf(keyOf(commandBuffer));
    if (d->followsFrames) {
        std::lock_guard lock(d->framesMutex);
        d->frames.reset(handleOf(commandBuffer));
    }
    return d->resetCommandBuffer(commandBuffer, flags);
}

VKAPI_ATTR VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer commandBuffer,
                                                  const VkCommandBufferBeginInfo* pBeginInfo) {
    VrsDevice* d = deviceOf(keyOf(commandBuffer));
    if (d->followsFrames) {
        std::lock_guard lock(d->framesMutex);
        d->frames.begin(handleOf(commandBuffer));
    }
    return d->beginCommandBuffer(commandBuffer, pBeginInfo);
}

} // namespace

void loadCommandBufferFunctions(VrsDevice& d) {
    DeviceData& data = *d.data;
    const auto next = [&](const char* name) {
        return data.nextGetDeviceProcAddr(data.device, name);
    };
    d.allocateCommandBuffers =
        reinterpret_cast<PFN_vkAllocateCommandBuffers>(next("vkAllocateCommandBuffers"));
    d.freeCommandBuffers = reinterpret_cast<PFN_vkFreeCommandBuffers>(next("vkFreeCommandBuffers"));
    d.destroyCommandPool = reinterpret_cast<PFN_vkDestroyCommandPool>(next("vkDestroyCommandPool"));
    d.resetCommandPool = reinterpret_cast<PFN_vkResetCommandPool>(next("vkResetCommandPool"));
    d.resetCommandBuffer = reinterpret_cast<PFN_vkResetCommandBuffer>(next("vkResetCommandBuffer"));
    d.beginCommandBuffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(next("vkBeginCommandBuffer"));
}

PFN_vkVoidFunction findCommandBufferHook(const char* name) {
#define EVR_VRS_CB_HOOK(fn)                                                                                  \
    if (std::strcmp(name, "vk" #fn) == 0) {                                                                  \
        return reinterpret_cast<PFN_vkVoidFunction>(&fn);                                                    \
    }
    EVR_VRS_CB_HOOK(AllocateCommandBuffers)
    EVR_VRS_CB_HOOK(FreeCommandBuffers)
    EVR_VRS_CB_HOOK(DestroyCommandPool)
    EVR_VRS_CB_HOOK(ResetCommandPool)
    EVR_VRS_CB_HOOK(ResetCommandBuffer)
    EVR_VRS_CB_HOOK(BeginCommandBuffer)
#undef EVR_VRS_CB_HOOK
    return nullptr;
}

} // namespace evr::vkcore::vrs_nv
