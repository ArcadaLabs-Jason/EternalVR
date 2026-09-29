// Stalls of the game's presents (stall_watch.hpp): the timing hooks, the per-gap accumulators, the stall
// line and its 10 s summary.

#include "vkcore/stall_watch.hpp"

#include "gpu_timing/present_stall.hpp"
#include "vkcore/log.hpp"
#include "vkcore/shader_dump.hpp"
#include "vkcore/vram_watch.hpp"
#include "vkcore/window_timing.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace evr::vkcore::stall_watch {

namespace {

namespace gt = ::evr::gpu_timing;

constexpr double kMiB = 1024.0 * 1024.0;

using DispatchKey = void*;

template <typename Handle>
DispatchKey keyOf(Handle handle) {
    return *reinterpret_cast<void**>(handle);
}

struct Next {
    bool game = false;
    PFN_vkCreateGraphicsPipelines createGraphicsPipelines = nullptr;
    PFN_vkCreateComputePipelines createComputePipelines = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
};

// Allocated once and never destroyed (see layer_entry.cpp: no teardown at process exit).
std::shared_mutex& g_devicesMutex = *new std::shared_mutex;
auto& g_devices = *new std::unordered_map<DispatchKey, Next>;

Next nextOf(DispatchKey key) {
    std::shared_lock lock(g_devicesMutex);
    const auto it = g_devices.find(key);
    return it == g_devices.end() ? Next{} : it->second;
}

void raiseMax(std::atomic<std::uint64_t>& max, std::uint64_t value) {
    std::uint64_t seen = max.load(std::memory_order_relaxed);
    while (value > seen && !max.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {
    }
}

// What was spent since the game's last present; each present takes (and resets) it, so a stall sees
// exactly its own gap.
struct Spent {
    std::uint64_t hookMicros = 0; // the layer's present hook, lock wait and driver present included
    std::uint64_t lockMicros = 0;
    std::uint64_t driverMicros = 0;
    std::uint64_t drainMicros = 0;
    std::uint64_t pipelines = 0;
    std::uint64_t pipelineCalls = 0;
    std::uint64_t pipelineMicros = 0;
    std::uint64_t pipelineMaxMicros = 0; // the longest call
    std::uint64_t allocations = 0;
    std::uint64_t allocationBytes = 0;
    std::uint64_t allocationMicros = 0;
};

struct AtomicSpent {
    std::atomic<std::uint64_t> hookMicros{0};
    std::atomic<std::uint64_t> lockMicros{0};
    std::atomic<std::uint64_t> driverMicros{0};
    std::atomic<std::uint64_t> drainMicros{0};
    std::atomic<std::uint64_t> pipelines{0};
    std::atomic<std::uint64_t> pipelineCalls{0};
    std::atomic<std::uint64_t> pipelineMicros{0};
    std::atomic<std::uint64_t> pipelineMaxMicros{0};
    std::atomic<std::uint64_t> allocations{0};
    std::atomic<std::uint64_t> allocationBytes{0};
    std::atomic<std::uint64_t> allocationMicros{0};

    Spent take() {
        const auto t = [](std::atomic<std::uint64_t>& a) {
            return a.exchange(0, std::memory_order_relaxed);
        };
        Spent s;
        s.hookMicros = t(hookMicros);
        s.lockMicros = t(lockMicros);
        s.driverMicros = t(driverMicros);
        s.drainMicros = t(drainMicros);
        s.pipelines = t(pipelines);
        s.pipelineCalls = t(pipelineCalls);
        s.pipelineMicros = t(pipelineMicros);
        s.pipelineMaxMicros = t(pipelineMaxMicros);
        s.allocations = t(allocations);
        s.allocationBytes = t(allocationBytes);
        s.allocationMicros = t(allocationMicros);
        return s;
    }
};
AtomicSpent g_spent;

std::atomic<std::uint64_t> g_lastPresent{0};   // entry time of the game's last present (0: none yet)
std::atomic<std::uint64_t> g_lastTick{0};      // time of the last game tick (0: none yet)
std::atomic<std::uint64_t> g_tickAtPresent{0}; // g_lastTick as the last present found it
std::atomic<bool> g_ticksKnown{false};
// Until the XR worker says whether the ticks are seen (trackGameTicks), the game is starting up or loading,
// so the stalls then are judged as if ticks were seen: with none yet, none of them is in play.
std::atomic<bool> g_ticksDecided{false};

// The gate and the summary's window, touched only for a stall and by the 10 s summary.
struct Stalls {
    std::mutex mutex;
    gt::StallGate gate;
    std::uint64_t summaryInPlay = 0; // the gate's counts at the last summary
    std::uint64_t summaryOutsidePlay = 0;
    double windowLongestMs = 0.0; // the longest stall in play since the last summary
};
Stalls& g_stalls = *new Stalls;

double ms(std::uint64_t micros) {
    return static_cast<double>(micros) / 1000.0;
}

void logStall(double gapMs, bool ticksKnown, std::uint32_t line, const Spent& s) {
    const vram::Reading v = vram::latest();
    char memory[64] = "unknown";
    if (v.valid) {
        std::snprintf(memory, sizeof(memory), "%.0f of %.0f MB", static_cast<double>(v.usage) / kMiB,
                      static_cast<double>(v.budget) / kMiB);
    }
    EVR_LOG("stall: %.1f ms between game presents (%s, line %u of %u); in the gap: our present hook %.1f ms "
            "(lock wait %.1f ms, driver present %.1f ms), Route S drain %.1f ms; the game created %llu "
            "pipeline(s) in %llu call(s), %.1f ms (longest call %.1f ms), and made %llu allocation(s) of "
            "%.1f MB in %.1f ms; VRAM %s",
            gapMs, ticksKnown ? "in play" : "phase unknown", line, gt::kStallLinesLogged, ms(s.hookMicros),
            ms(s.lockMicros), ms(s.driverMicros), ms(s.drainMicros),
            static_cast<unsigned long long>(s.pipelines), static_cast<unsigned long long>(s.pipelineCalls),
            ms(s.pipelineMicros), ms(s.pipelineMaxMicros), static_cast<unsigned long long>(s.allocations),
            static_cast<double>(s.allocationBytes) / kMiB, ms(s.allocationMicros), memory);
}

// A gap over the threshold: the gate decides whether it gets a line.
void onStall(double gapMs,
             std::uint64_t gapStart,
             std::uint64_t tickBefore,
             std::uint64_t lastTick,
             const Spent& spent) {
    gt::PresentGap gap;
    gap.gapMs = gapMs;
    gap.ticksKnown =
        !g_ticksDecided.load(std::memory_order_relaxed) || g_ticksKnown.load(std::memory_order_relaxed);
    gap.tickAgeAtStartMs = tickBefore != 0 && tickBefore <= gapStart ? ms(gapStart - tickBefore) : -1.0;
    gap.tickDuring = lastTick > tickBefore;
    std::lock_guard lock(g_stalls.mutex);
    const gt::StallVerdict verdict = g_stalls.gate.onGap(gap);
    if (verdict == gt::StallVerdict::OutsidePlay) {
        return;
    }
    if (gapMs > g_stalls.windowLongestMs) {
        g_stalls.windowLongestMs = gapMs;
    }
    if (verdict == gt::StallVerdict::Log || verdict == gt::StallVerdict::LastLog) {
        logStall(gapMs, gap.ticksKnown, g_stalls.gate.counters().logged, spent);
    }
    if (verdict == gt::StallVerdict::LastLog) {
        EVR_LOG("stall: %u stall lines logged; later stalls are only counted in the 10 s stall summary",
                gt::kStallLinesLogged);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL CreateGraphicsPipelines(VkDevice device,
                                                       VkPipelineCache cache,
                                                       std::uint32_t count,
                                                       const VkGraphicsPipelineCreateInfo* pInfos,
                                                       const VkAllocationCallbacks* pAllocator,
                                                       VkPipeline* pPipelines) {
    const Next next = nextOf(keyOf(device));
    if (!next.game) {
        return next.createGraphicsPipelines(device, cache, count, pInfos, pAllocator, pPipelines);
    }
    const std::uint64_t start = window_timing::nowMicros();
    const VkResult result =
        next.createGraphicsPipelines(device, cache, count, pInfos, pAllocator, pPipelines);
    const std::uint64_t took = window_timing::nowMicros() - start;
    g_spent.pipelines.fetch_add(count, std::memory_order_relaxed);
    g_spent.pipelineCalls.fetch_add(1, std::memory_order_relaxed);
    g_spent.pipelineMicros.fetch_add(took, std::memory_order_relaxed);
    raiseMax(g_spent.pipelineMaxMicros, took);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateComputePipelines(VkDevice device,
                                                      VkPipelineCache cache,
                                                      std::uint32_t count,
                                                      const VkComputePipelineCreateInfo* pInfos,
                                                      const VkAllocationCallbacks* pAllocator,
                                                      VkPipeline* pPipelines) {
    const Next next = nextOf(keyOf(device));
    if (!next.game) {
        return next.createComputePipelines(device, cache, count, pInfos, pAllocator, pPipelines);
    }
    const std::uint64_t start = window_timing::nowMicros();
    const VkResult result = next.createComputePipelines(device, cache, count, pInfos, pAllocator, pPipelines);
    const std::uint64_t took = window_timing::nowMicros() - start;
    g_spent.pipelines.fetch_add(count, std::memory_order_relaxed);
    g_spent.pipelineCalls.fetch_add(1, std::memory_order_relaxed);
    g_spent.pipelineMicros.fetch_add(took, std::memory_order_relaxed);
    raiseMax(g_spent.pipelineMaxMicros, took);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL AllocateMemory(VkDevice device,
                                              const VkMemoryAllocateInfo* pAllocateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkDeviceMemory* pMemory) {
    const Next next = nextOf(keyOf(device));
    if (!next.game) {
        return next.allocateMemory(device, pAllocateInfo, pAllocator, pMemory);
    }
    const std::uint64_t start = window_timing::nowMicros();
    const VkResult result = next.allocateMemory(device, pAllocateInfo, pAllocator, pMemory);
    g_spent.allocationMicros.fetch_add(window_timing::nowMicros() - start, std::memory_order_relaxed);
    g_spent.allocations.fetch_add(1, std::memory_order_relaxed);
    g_spent.allocationBytes.fetch_add(pAllocateInfo->allocationSize, std::memory_order_relaxed);
    return result;
}

// The hook of the same function further along this layer's own chain, else the next layer's function.
PFN_vkVoidFunction chained(DeviceData& data, const char* name) {
    if (const PFN_vkVoidFunction dump = shader_dump::findHook(name)) {
        return dump;
    }
    return data.nextGetDeviceProcAddr(data.device, name);
}

} // namespace

void onDeviceCreated(DeviceData& data, bool isGame) {
    Next next;
    next.game = isGame;
    next.createGraphicsPipelines =
        reinterpret_cast<PFN_vkCreateGraphicsPipelines>(chained(data, "vkCreateGraphicsPipelines"));
    next.createComputePipelines =
        reinterpret_cast<PFN_vkCreateComputePipelines>(chained(data, "vkCreateComputePipelines"));
    next.allocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(chained(data, "vkAllocateMemory"));
    std::unique_lock lock(g_devicesMutex);
    g_devices[keyOf(data.device)] = next;
}

void onDeviceDestroyed(VkDevice device) {
    std::unique_lock lock(g_devicesMutex);
    g_devices.erase(keyOf(device));
}

PFN_vkVoidFunction findHook(const char* name) {
#define EVR_HOOK(fn)                                                                                         \
    if (std::strcmp(name, "vk" #fn) == 0) {                                                                  \
        return reinterpret_cast<PFN_vkVoidFunction>(&fn);                                                    \
    }
    EVR_HOOK(CreateGraphicsPipelines)
    EVR_HOOK(CreateComputePipelines)
    EVR_HOOK(AllocateMemory)
#undef EVR_HOOK
    return nullptr;
}

std::uint64_t presentEntered() {
    const std::uint64_t now = window_timing::nowMicros();
    const std::uint64_t previous = g_lastPresent.exchange(now, std::memory_order_relaxed);
    const std::uint64_t lastTick = g_lastTick.load(std::memory_order_relaxed);
    const std::uint64_t tickBefore = g_tickAtPresent.exchange(lastTick, std::memory_order_relaxed);
    const Spent spent = g_spent.take();
    if (previous != 0 && now > previous && ms(now - previous) > gt::kStallGapMs) {
        onStall(ms(now - previous), previous, tickBefore, lastTick, spent);
    }
    return now;
}

void presentLeft(std::uint64_t entered) {
    g_spent.hookMicros.fetch_add(window_timing::nowMicros() - entered, std::memory_order_relaxed);
}

void addLockWait(std::uint64_t micros) {
    g_spent.lockMicros.fetch_add(micros, std::memory_order_relaxed);
}

void addDriverPresent(std::uint64_t micros) {
    g_spent.driverMicros.fetch_add(micros, std::memory_order_relaxed);
}

void addDrain(std::uint64_t micros) {
    g_spent.drainMicros.fetch_add(micros, std::memory_order_relaxed);
}

void onGameTick() {
    g_lastTick.store(window_timing::nowMicros(), std::memory_order_relaxed);
}

void trackGameTicks(bool on) {
    g_ticksKnown.store(on, std::memory_order_relaxed);
    g_ticksDecided.store(true, std::memory_order_relaxed);
    EVR_LOG("stall: game presents more than %.0f ms apart get a line (the first %u)%s", gt::kStallGapMs,
            gt::kStallLinesLogged,
            on ? "; loading screens and menus are only counted"
               : "; the game's ticks are not seen, so loading screens and menus count too");
}

void logSummary() {
    std::lock_guard lock(g_stalls.mutex);
    const gt::StallGate::Counters& c = g_stalls.gate.counters();
    const std::uint64_t inPlay = c.inPlay - g_stalls.summaryInPlay;
    const std::uint64_t outside = c.outsidePlay - g_stalls.summaryOutsidePlay;
    if (inPlay != 0 || outside != 0) {
        EVR_LOG("stall: last 10 s: %llu stall(s) in play (longest %.1f ms), %llu on loading screens or in "
                "menus; %llu in play in total, %u logged",
                static_cast<unsigned long long>(inPlay), g_stalls.windowLongestMs,
                static_cast<unsigned long long>(outside), static_cast<unsigned long long>(c.inPlay),
                c.logged);
    }
    g_stalls.summaryInPlay = c.inPlay;
    g_stalls.summaryOutsidePlay = c.outsidePlay;
    g_stalls.windowLongestMs = 0.0;
}

} // namespace evr::vkcore::stall_watch
