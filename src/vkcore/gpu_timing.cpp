// GPU timing (gpu_timing.hpp): devices, the vkQueueSubmit hook and the command buffers that bracket each
// batch with a timestamp pair. Reading results and the summaries are in gpu_timing_report.cpp.

#include "vkcore/gpu_timing.hpp"

#include "gpu_timing/stats.hpp"
#include "vkcore/gpu_timing_impl.hpp"
#include "vkcore/hook_guard.hpp"
#include "vkcore/log.hpp"
#include "vkcore/next_function.hpp"
#include "vkcore/ui_vulkan.hpp"

#include <atomic>
#include <cstring>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <utility>

namespace evr::vkcore::gpu_timing {

namespace {

using DispatchKey = void*;

template <typename Handle>
DispatchKey keyOf(Handle handle) {
    return *reinterpret_cast<void**>(handle);
}

// Allocated once and never destroyed (see layer_entry.cpp: no teardown at process exit).
std::shared_mutex& g_devicesMutex = *new std::shared_mutex;
auto& g_devices = *new std::unordered_map<DispatchKey, std::unique_ptr<TimingDevice>>;

// The frame the last present on this thread closed, for the presenter's eye tag.
struct ClosedFrame {
    DispatchKey key = nullptr;
    std::uint64_t id = 0;
};
thread_local ClosedFrame t_closed;

// The submit hook and the present's frame close run inside the game's calls: an exception leaves neither
// (hook_guard.hpp). A submit that cannot be planned goes untimed, a frame that cannot be closed or read is
// lost, and a device whose record cannot be made turns GPU timing off (its hook would find no record).
HookGuard g_modeGuard("GPU timing's mode");
HookGuard g_deviceGuard("GPU timing's device record");
HookGuard g_submitGuard("vkQueueSubmit (GPU timing)");
HookGuard g_presentGuard("GPU timing's frame close at the present");
HookGuard g_poseGuard("GPU timing's pose age");
std::atomic<bool> g_failed{false};

TimingDevice* deviceOf(DispatchKey key) {
    std::shared_lock lock(g_devicesMutex);
    const auto it = g_devices.find(key);
    return it == g_devices.end() ? nullptr : it->second.get();
}

gt::TimingMode mode() {
    static const gt::TimingMode m = [] {
        gt::TimingMode read = gt::TimingMode::Off; // off when the variable cannot be read
        g_modeGuard.run([&] {
            std::wstring value;
            const bool set = readEnv(L"ETERNALVR_GPU_TIMING", value);
            read = gt::parseMode(set, value);
        });
        return read;
    }();
    return m;
}

// Under d.mutex: the queue's family (from the layer's queue records, cached).
std::optional<std::uint32_t> familyOf(TimingDevice& d, VkQueue queue) {
    const auto it = d.queueFamilies.find(queue);
    if (it != d.queueFamilies.end()) {
        return it->second;
    }
    std::lock_guard lock(d.data->queueMutex);
    const auto known = d.data->queueFamilies.find(queue);
    if (known == d.data->queueFamilies.end()) {
        return std::nullopt;
    }
    d.queueFamilies.emplace(queue, known->second);
    return known->second;
}

// Under d.mutex: the family's command pool (created on first use); nullptr when it cannot be made or the
// family cannot reset queries (transfer-only queues).
FamilyTimer* timerFor(TimingDevice& d, std::uint32_t family) {
    const auto it = d.timers.find(family);
    if (it != d.timers.end()) {
        return it->second.pool ? &it->second : nullptr;
    }
    FamilyTimer t;
    const VkQueueFlags flags =
        family < d.data->queueFamilyFlags.size() ? d.data->queueFamilyFlags[family] : 0;
    VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    info.queueFamilyIndex = family;
    if (!(flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ||
        d.fn.createCommandPool(d.device, &info, nullptr, &t.pool) != VK_SUCCESS) {
        EVR_LOG("gpu: queue family %u (flags 0x%x) cannot be timed; its submits go untimed", family, flags);
        t.pool = VK_NULL_HANDLE;
        d.timers.emplace(family, t);
        return nullptr;
    }
    t.begin.assign(kPairs, VK_NULL_HANDLE);
    t.end.assign(kPairs, VK_NULL_HANDLE);
    EVR_LOG("gpu: timing submits on queue family %u (%u valid timestamp bits)", family, d.validBits[family]);
    return &d.timers.emplace(family, std::move(t)).first->second;
}

// A command buffer from `pool` holding what `record` records; VK_NULL_HANDLE on failure.
template <typename Record>
VkCommandBuffer recordOnce(TimingDevice& d, VkCommandPool pool, Record&& record) {
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (d.fn.allocateCommandBuffers(d.device, &alloc, &cb) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    // Command buffers made by a layer need the loader's dispatch pointer.
    d.data->setDeviceLoaderData(d.device, cb);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    // A pair is reused only after its results were read, but a batch given up on may still be pending.
    begin.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    if (d.fn.beginCommandBuffer(cb, &begin) != VK_SUCCESS) {
        return VK_NULL_HANDLE; // freed with the pool
    }
    record(cb);
    return d.fn.endCommandBuffer(cb) == VK_SUCCESS ? cb : VK_NULL_HANDLE;
}

// Resets `query` and writes a timestamp into it at `stage`.
VkCommandBuffer
recordStamp(TimingDevice& d, VkCommandPool pool, std::uint32_t query, VkPipelineStageFlagBits stage) {
    return recordOnce(d, pool, [&](VkCommandBuffer cb) {
        d.fn.cmdResetQueryPool(cb, d.pool, query, 1);
        d.fn.cmdWriteTimestamp(cb, stage, d.pool, query);
    });
}

// Under d.mutex: the pair's two command buffers for `t`'s family, recorded on first use.
bool stampsFor(TimingDevice& d, FamilyTimer& t, std::uint32_t pair) {
    if (!t.begin[pair]) {
        t.begin[pair] =
            recordStamp(d, t.pool, gt::QueryRing::beginQuery(pair), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    }
    if (!t.end[pair]) {
        t.end[pair] =
            recordStamp(d, t.pool, gt::QueryRing::endQuery(pair), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
    return t.begin[pair] && t.end[pair];
}

// Under d.mutex: the command buffer that resets the whole pool, for the first timed batch.
VkCommandBuffer resetAllFor(TimingDevice& d, FamilyTimer& t) {
    if (!t.resetAll) {
        t.resetAll = recordOnce(d, t.pool, [&](VkCommandBuffer cb) {
            d.fn.cmdResetQueryPool(cb, d.pool, 0, d.ring.queryCount());
        });
    }
    return t.resetAll;
}

// A batch that can take more command buffers: nothing in its chain is sized by its command buffer count
// (device groups) or needs protected command buffers.
bool bracketable(const VkSubmitInfo& info) {
    for (auto* s = static_cast<const VkBaseInStructure*>(info.pNext); s; s = s->pNext) {
        if (s->sType == VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO ||
            s->sType == VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO) {
            return false;
        }
    }
    return true;
}

VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit(VkQueue queue,
                                           std::uint32_t submitCount,
                                           const VkSubmitInfo* pSubmits,
                                           VkFence fence) {
    TimingDevice* d = deviceOf(keyOf(queue));
    if (!d) { // no record (no memory for it): the next layer's submit, untimed
        const auto next = nextFunction<PFN_vkQueueSubmit>(queue, "vkQueueSubmit");
        return next ? next(queue, submitCount, pSubmits, fence) : VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (!d->active || submitCount == 0 || !d->sampling.load(std::memory_order_relaxed)) {
        return d->fn.queueSubmit(queue, submitCount, pSubmits, fence);
    }
    std::vector<std::uint32_t> planned; // the pairs of the timed batches
    std::vector<VkSubmitInfo> infos;
    std::vector<VkCommandBuffer> buffers;
    std::uint32_t family = 0;
    bool resetsPool = false;
    // Everything the plan allocates comes before its first pair is taken (`planned` and `buffers` reserved),
    // so a plan that throws has taken none: the game's batches then go as they are, untimed.
    const bool ready = g_submitGuard.run([&] {
        planned.reserve(submitCount);
        infos.assign(pSubmits, pSubmits + submitCount);
        std::lock_guard lock(d->mutex);
        const std::optional<std::uint32_t> f = familyOf(*d, queue);
        FamilyTimer* t = f && *f < d->validBits.size() && d->validBits[*f] ? timerFor(*d, *f) : nullptr;
        family = f.value_or(0);
        std::size_t total = 1;
        for (std::uint32_t i = 0; i < submitCount; ++i) {
            total += pSubmits[i].commandBufferCount + 2;
        }
        buffers.reserve(total); // no reallocation: the batches point into it
        // A present may have closed the sampled frame since `sampling` was read: then nothing is timed.
        for (std::uint32_t i = 0; i < submitCount && d->open.timed; ++i) {
            const VkSubmitInfo& s = pSubmits[i];
            if (s.commandBufferCount == 0) {
                continue; // semaphores only: no work to time
            }
            std::uint64_t* skipped = !t                ? &d->counters.noTimestamps
                                     : !bracketable(s) ? &d->counters.notBracketable
                                     : d->open.batches.size() + planned.size() >= kMaxBatchesPerFrame
                                         ? &d->counters.frameCap
                                         : nullptr;
            std::optional<std::uint32_t> pair;
            if (!skipped) {
                pair = d->ring.acquire();
                skipped = pair ? nullptr : &d->counters.ringFull;
            }
            const bool needsReset = pair && !d->poolReset;
            if (pair && (!stampsFor(*d, *t, *pair) || (needsReset && !resetAllFor(*d, *t)))) {
                d->ring.release(*pair);
                skipped = &d->counters.noTimestamps;
            }
            if (skipped) {
                ++*skipped;
                ++d->open.untimed;
                continue;
            }
            const std::size_t first = buffers.size();
            if (needsReset) {
                // Queries start uninitialized: this reset runs before any of them is written or read.
                buffers.push_back(t->resetAll);
                resetsPool = true;
                d->poolReset = true;
            }
            buffers.push_back(t->begin[*pair]);
            buffers.insert(buffers.end(), s.pCommandBuffers, s.pCommandBuffers + s.commandBufferCount);
            buffers.push_back(t->end[*pair]);
            infos[i].commandBufferCount = static_cast<std::uint32_t>(buffers.size() - first);
            infos[i].pCommandBuffers = buffers.data() + first;
            planned.push_back(*pair);
        }
    });
    if (!ready || planned.empty()) {
        return d->fn.queueSubmit(queue, submitCount, pSubmits, fence);
    }
    const VkResult result = d->fn.queueSubmit(queue, submitCount, infos.data(), fence);
    std::lock_guard lock(d->mutex);
    std::size_t recorded = 0;
    if (result == VK_SUCCESS) {
        // Submitted once only: a batch whose record finds no memory gives its pair back below.
        g_submitGuard.run([&] {
            for (; recorded < planned.size(); ++recorded) {
                d->open.batches.push_back({planned[recorded], family});
            }
        });
    }
    for (std::size_t i = recorded; i < planned.size(); ++i) {
        d->ring.release(planned[i]);
    }
    if (result != VK_SUCCESS && resetsPool) {
        d->poolReset = false;
    }
    return result;
}

void destroyObjects(TimingDevice& d) {
    for (auto& [family, t] : d.timers) {
        if (t.pool) {
            d.fn.destroyCommandPool(d.device, t.pool, nullptr);
        }
    }
    d.timers.clear();
    if (d.pool) {
        d.fn.destroyQueryPool(d.device, d.pool, nullptr);
        d.pool = VK_NULL_HANDLE;
    }
}

} // namespace

bool enabled() {
    return mode() != gt::TimingMode::Off;
}

namespace {

// onDeviceCreated's work, under its guard.
void addDevice(DeviceData& data,
               const VkPhysicalDeviceProperties& properties,
               const std::vector<VkQueueFamilyProperties>& families,
               bool isGame) {
    auto d = std::make_unique<TimingDevice>();
    d->device = data.device;
    d->data = &data;
    const PFN_vkGetDeviceProcAddr gdpa = data.nextGetDeviceProcAddr;
    const auto load = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(gdpa(data.device, name));
        return fn != nullptr;
    };
    // The UI layer hooks vkQueueSubmit too: with both on, ours calls its hook, which calls the next.
    const PFN_vkVoidFunction ui = ui_vulkan::findHook("vkQueueSubmit");
    d->fn.queueSubmit = ui ? reinterpret_cast<PFN_vkQueueSubmit>(ui)
                           : reinterpret_cast<PFN_vkQueueSubmit>(gdpa(data.device, "vkQueueSubmit"));
    Functions& f = d->fn;
    const bool loaded =
        load(f.createQueryPool, "vkCreateQueryPool") && load(f.destroyQueryPool, "vkDestroyQueryPool") &&
        load(f.getQueryPoolResults, "vkGetQueryPoolResults") &&
        load(f.createCommandPool, "vkCreateCommandPool") &&
        load(f.destroyCommandPool, "vkDestroyCommandPool") &&
        load(f.allocateCommandBuffers, "vkAllocateCommandBuffers") &&
        load(f.beginCommandBuffer, "vkBeginCommandBuffer") &&
        load(f.endCommandBuffer, "vkEndCommandBuffer") && load(f.cmdResetQueryPool, "vkCmdResetQueryPool") &&
        load(f.cmdWriteTimestamp, "vkCmdWriteTimestamp") && f.queueSubmit != nullptr;
    std::string bits;
    bool anyBits = false;
    for (const VkQueueFamilyProperties& family : families) {
        d->validBits.push_back(family.timestampValidBits);
        anyBits = anyBits || family.timestampValidBits > 0;
        bits += (bits.empty() ? "" : " ") + std::to_string(family.timestampValidBits);
    }
    d->periodNs = properties.limits.timestampPeriod;
    d->everyFrame = mode() == gt::TimingMode::EveryFrame;
    d->open.timed = d->everyFrame || gt::sampledFrame(1);
    d->sampling.store(d->open.timed, std::memory_order_relaxed);
    if (isGame) {
        EVR_LOG("gpu: GPU timing requested (%s): timestamp period %.3f ns, valid bits per queue family [%s], "
                "compute and graphics %s",
                d->everyFrame
                    ? "every frame, ETERNALVR_GPU_TIMING=1"
                    : "sampled, the default; ETERNALVR_GPU_TIMING=1 times every frame, =0 turns it off",
                static_cast<double>(d->periodNs), bits.c_str(),
                properties.limits.timestampComputeAndGraphics ? "yes" : "no");
    }
    if (isGame && loaded && anyBits && data.setDeviceLoaderData && d->periodNs > 0.0f) {
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = d->ring.queryCount();
        if (f.createQueryPool(data.device, &info, nullptr, &d->pool) == VK_SUCCESS) {
            d->active = true;
            d->lastReport = Clock::now();
            if (d->everyFrame) {
                openCsv(*d);
            }
            EVR_LOG(
                "gpu: GPU timing on: %u query pairs; the submit batches of %s are bracketed with timestamps",
                kPairs, d->everyFrame ? "every frame" : "the sampled frames");
        } else {
            EVR_LOG("gpu: vkCreateQueryPool failed; GPU timing off");
        }
    } else if (isGame) {
        EVR_LOG("gpu: GPU timing off (%s)", !loaded    ? "a device function is missing"
                                            : !anyBits ? "no queue family has timestamps"
                                                       : "no loader callback or timestamp period");
    }
    std::unique_lock lock(g_devicesMutex);
    g_devices[keyOf(data.device)] = std::move(d);
}

} // namespace

void onDeviceCreated(DeviceData& data,
                     const VkPhysicalDeviceProperties& properties,
                     const std::vector<VkQueueFamilyProperties>& families,
                     bool isGame) {
    if (!enabled() || g_failed.load()) {
        return;
    }
    const bool recorded = g_deviceGuard.run([&] { addDevice(data, properties, families, isGame); });
    if (!recorded) {
        g_failed.store(true, std::memory_order_release);
        EVR_LOG("gpu: GPU timing's hook is off from here (a device could not be recorded)");
    }
}

void onDeviceDestroyed(VkDevice device) {
    if (!enabled()) {
        return;
    }
    std::unique_ptr<TimingDevice> d;
    {
        std::unique_lock lock(g_devicesMutex);
        const auto it = g_devices.find(keyOf(device));
        if (it == g_devices.end()) {
            return;
        }
        d = std::move(it->second);
        g_devices.erase(it);
    }
    std::lock_guard lock(d->mutex);
    if (d->active) {
        destroyObjects(*d); // the application has finished its work on the device before destroying it
        closeCsv(*d);
    }
}

PFN_vkVoidFunction findHook(const char* name) {
    if (!enabled() || g_failed.load(std::memory_order_acquire) || std::strcmp(name, "vkQueueSubmit") != 0) {
        return nullptr;
    }
    return reinterpret_cast<PFN_vkVoidFunction>(&QueueSubmit);
}

void onPresent(VkQueue queue) {
    if (!enabled()) {
        return;
    }
    TimingDevice* d = deviceOf(keyOf(queue));
    if (!d || !d->active) {
        return;
    }
    const Clock::time_point now = Clock::now();
    std::lock_guard lock(d->mutex);
    g_presentGuard.run([&] { // the closed list grows, the reads and the summary allocate
        Frame frame = std::move(d->open);
        d->open = Frame{};
        frame.id = ++d->presents;
        if (frame.id > 1) {
            frame.cpuMs = std::chrono::duration<double, std::milli>(now - d->lastPresent).count();
        }
        d->lastPresent = now;
        d->closed.push_back(std::move(frame));
        d->open.timed = d->everyFrame || gt::sampledFrame(d->presents + 1);
        d->sampling.store(d->open.timed, std::memory_order_relaxed);
        t_closed = ClosedFrame{keyOf(queue), d->presents};
        resolveFrames(*d);
        reportIfDue(*d, now);
    });
}

void tagEye(stereo_seq::Eye eye) {
    if (!enabled() || !t_closed.key) {
        return;
    }
    TimingDevice* d = deviceOf(t_closed.key);
    if (!d) {
        return;
    }
    std::lock_guard lock(d->mutex);
    for (auto it = d->closed.rbegin(); it != d->closed.rend(); ++it) {
        if (it->id == t_closed.id) {
            it->eye = static_cast<gt::FrameEye>(eye);
            break;
        }
    }
}

void notePoseAge(double ms) {
    if (mode() != gt::TimingMode::EveryFrame) {
        return; // only the full summary has it
    }
    std::shared_lock devicesLock(g_devicesMutex);
    for (auto& [key, d] : g_devices) {
        if (d->active) {
            std::lock_guard lock(d->mutex);
            if (d->poseAges.size() < 100000) {
                std::vector<double>& ages = d->poseAges;
                g_poseGuard.run([&] { ages.push_back(ms); });
            }
        }
    }
}

} // namespace evr::vkcore::gpu_timing
