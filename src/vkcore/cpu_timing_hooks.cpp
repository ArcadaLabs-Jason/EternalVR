// CPU timing's Vulkan call hooks (cpu_timing.hpp): vkQueueSubmit, vkWaitForFences and vkWaitSemaphores[KHR]
// on the game's device, each timed as a stage, the submits also counted for the census (cpu_timing.cpp).

#include "vkcore/cpu_timing.hpp"
#include "vkcore/cpu_timing_impl.hpp"

#include "vkcore/gpu_timing.hpp"
#include "vkcore/hook_guard.hpp"
#include "vkcore/log.hpp"
#include "vkcore/next_function.hpp"
#include "vkcore/shader_dump.hpp"
#include "vkcore/ui_vulkan.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace evr::vkcore::cpu_timing {

namespace {

namespace gt = ::evr::gpu_timing;
using gt::CpuStage;

using DispatchKey = void*;

template <typename Handle>
DispatchKey keyOf(Handle handle) {
    return *reinterpret_cast<void**>(handle);
}

struct Next {
    bool game = false;
    PFN_vkQueueSubmit queueSubmit = nullptr;
    PFN_vkWaitForFences waitForFences = nullptr;
    PFN_vkWaitSemaphores waitSemaphores = nullptr;
    PFN_vkWaitSemaphores waitSemaphoresKhr = nullptr;
};

// Allocated once and never destroyed (see layer_entry.cpp: no teardown at process exit).
std::shared_mutex& g_devicesMutex = *new std::shared_mutex;
auto& g_devices = *new std::unordered_map<DispatchKey, Next>;

// The hooks run inside the game's calls: an exception leaves none of them (hook_guard.hpp). What fails is
// skipped: a submit not counted, a device not recorded.
HookGuard g_deviceGuard("CPU timing's device record");
HookGuard g_submitGuard("vkQueueSubmit (CPU timing's submit census)");
// A device's record could not be made: it would reach the hooks without its next functions, so none is handed
// out from then on.
std::atomic<bool> g_failed{false};

// ---- Vulkan call hooks ----

// The device's record; for a device without one (no memory for it), the next layer's functions, untimed.
template <typename Handle>
Next nextOf(Handle handle) {
    {
        std::shared_lock lock(g_devicesMutex);
        const auto it = g_devices.find(keyOf(handle));
        if (it != g_devices.end()) {
            return it->second;
        }
    }
    Next next;
    next.queueSubmit = nextFunction<PFN_vkQueueSubmit>(handle, "vkQueueSubmit");
    next.waitForFences = nextFunction<PFN_vkWaitForFences>(handle, "vkWaitForFences");
    next.waitSemaphores = nextFunction<PFN_vkWaitSemaphores>(handle, "vkWaitSemaphores");
    next.waitSemaphoresKhr = nextFunction<PFN_vkWaitSemaphores>(handle, "vkWaitSemaphoresKHR");
    return next;
}

VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit(VkQueue queue,
                                           std::uint32_t submitCount,
                                           const VkSubmitInfo* pSubmits,
                                           VkFence fence) {
    const Next next = nextOf(queue);
    if (!next.game) {
        return next.queueSubmit ? next.queueSubmit(queue, submitCount, pSubmits, fence)
                                : VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    g_submitGuard.run([&] { detail::recordSubmit(queue, submitCount, pSubmits, fence); });
    Scope scope(CpuStage::Submit);
    return next.queueSubmit(queue, submitCount, pSubmits, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL WaitForFences(VkDevice device,
                                             std::uint32_t fenceCount,
                                             const VkFence* pFences,
                                             VkBool32 waitAll,
                                             std::uint64_t timeout) {
    const Next next = nextOf(device);
    if (!next.waitForFences) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (!next.game || timeout == 0) {
        return next.waitForFences(device, fenceCount, pFences, waitAll, timeout);
    }
    Scope scope(CpuStage::FenceWait);
    return next.waitForFences(device, fenceCount, pFences, waitAll, timeout);
}

VKAPI_ATTR VkResult VKAPI_CALL WaitSemaphores(VkDevice device,
                                              const VkSemaphoreWaitInfo* pWaitInfo,
                                              std::uint64_t timeout) {
    const Next next = nextOf(device);
    if (!next.waitSemaphores) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (!next.game || timeout == 0) {
        return next.waitSemaphores(device, pWaitInfo, timeout);
    }
    Scope scope(CpuStage::SemaphoreWait);
    return next.waitSemaphores(device, pWaitInfo, timeout);
}

VKAPI_ATTR VkResult VKAPI_CALL WaitSemaphoresKHR(VkDevice device,
                                                 const VkSemaphoreWaitInfo* pWaitInfo,
                                                 std::uint64_t timeout) {
    const Next next = nextOf(device);
    if (!next.waitSemaphoresKhr) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (!next.game || timeout == 0) {
        return next.waitSemaphoresKhr(device, pWaitInfo, timeout);
    }
    Scope scope(CpuStage::SemaphoreWait);
    return next.waitSemaphoresKhr(device, pWaitInfo, timeout);
}

// The hook of the same function further along this layer's own chain, else the next layer's function.
PFN_vkVoidFunction chained(DeviceData& data, const char* name) {
    if (const PFN_vkVoidFunction gpu = gpu_timing::findHook(name)) {
        return gpu;
    }
    if (const PFN_vkVoidFunction ui = ui_vulkan::findHook(name)) {
        return ui;
    }
    if (const PFN_vkVoidFunction dump = shader_dump::findHook(name)) {
        return dump;
    }
    return data.nextGetDeviceProcAddr(data.device, name);
}

} // namespace

void onDeviceCreated(DeviceData& data, bool isGame) {
    if (!enabled() || g_failed.load()) {
        return;
    }
    const bool recorded = g_deviceGuard.run([&] {
        Next next;
        next.game = isGame;
        next.queueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(chained(data, "vkQueueSubmit"));
        next.waitForFences = reinterpret_cast<PFN_vkWaitForFences>(chained(data, "vkWaitForFences"));
        next.waitSemaphores = reinterpret_cast<PFN_vkWaitSemaphores>(chained(data, "vkWaitSemaphores"));
        next.waitSemaphoresKhr = reinterpret_cast<PFN_vkWaitSemaphores>(chained(data, "vkWaitSemaphoresKHR"));
        if (isGame) {
            EVR_LOG("cpu: timing vkQueueSubmit, vkWaitForFences and vkWaitSemaphores%s on the game's device",
                    next.waitSemaphoresKhr ? " (and the KHR alias)" : "");
        }
        std::unique_lock lock(g_devicesMutex);
        g_devices[keyOf(data.device)] = next;
    });
    if (!recorded) {
        g_failed.store(true, std::memory_order_release);
        EVR_LOG("cpu: CPU timing's hooks are off from here (a device could not be recorded)");
    }
}

void onDeviceDestroyed(VkDevice device) {
    if (!enabled()) {
        return;
    }
    std::unique_lock lock(g_devicesMutex);
    g_devices.erase(keyOf(device));
}

PFN_vkVoidFunction findHook(const char* name) {
    if (!enabled() || g_failed.load(std::memory_order_acquire)) {
        return nullptr;
    }
#define EVR_HOOK(fn)                                                                                         \
    if (std::strcmp(name, "vk" #fn) == 0) {                                                                  \
        return reinterpret_cast<PFN_vkVoidFunction>(&(fn));                                                  \
    }
    EVR_HOOK(QueueSubmit)
    EVR_HOOK(WaitForFences)
    EVR_HOOK(WaitSemaphores)
    EVR_HOOK(WaitSemaphoresKHR)
#undef EVR_HOOK
    return nullptr;
}

} // namespace evr::vkcore::cpu_timing
