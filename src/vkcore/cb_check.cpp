// Parallel Eye Rendering rig tool: the game's command buffer use (cb_check.hpp).

#include "vkcore/cb_check.hpp"

#include "gpu_timing/stats.hpp"
#include "vkcore/cb_view_skip.hpp"
#include "vkcore/log.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace evr::vkcore::cb_check {

namespace {

const std::byte* gameBase() {
    static const auto* base = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    return base;
}

} // namespace

void describeAddress(void* address, char* out, std::size_t size) {
    const auto a = reinterpret_cast<std::uintptr_t>(address);
    const auto base = reinterpret_cast<std::uintptr_t>(gameBase());
    if (a >= base && a < base + 0x10000000) {
        std::snprintf(out, size, "G%llx", static_cast<unsigned long long>(a - base));
    } else {
        std::snprintf(out, size, "%p", address);
    }
}

namespace {

using DispatchKey = void*;

template <typename Handle>
DispatchKey keyOf(Handle handle) {
    return *reinterpret_cast<void**>(handle);
}

struct Next {
    DispatchKey key = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer endCommandBuffer = nullptr;
    PFN_vkResetCommandBuffer resetCommandBuffer = nullptr;
    PFN_vkCmdBeginRenderPass cmdBeginRenderPass = nullptr;
    PFN_vkCmdEndRenderPass cmdEndRenderPass = nullptr;
    PFN_vkCmdBindPipeline cmdBindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets cmdBindDescriptorSets = nullptr;
    PFN_vkCmdDraw cmdDraw = nullptr;
    PFN_vkCmdDrawIndexed cmdDrawIndexed = nullptr;
    PFN_vkCmdDispatch cmdDispatch = nullptr;
    PFN_vkCmdDrawIndirect cmdDrawIndirect = nullptr;
    PFN_vkCmdDrawIndexedIndirect cmdDrawIndexedIndirect = nullptr;
    PFN_vkCmdDispatchIndirect cmdDispatchIndirect = nullptr;
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier = nullptr;
    PFN_vkQueueSubmit queueSubmit = nullptr;
};

// The game's device, set once at its creation (the check watches one device).
Next g_next;
std::atomic<bool> g_on{false};

bool readEnabled() {
    std::wstring value;
    return readEnv(L"ETERNALVR_TEST_CB_CHECK", value) && ::evr::gpu_timing::parseEnabled(value);
}

// ---- Per command buffer state ----

struct State {
    std::atomic<std::uint32_t> inCall{0};     // the thread inside a call on the buffer
    std::atomic<void*> inCallFrom{nullptr};   // its caller
    std::atomic<std::uint32_t> recorder{0};   // the thread of the last recorded command
    std::atomic<void*> recorderFrom{nullptr}; // its caller
    std::atomic<bool> recording{false};       // between begin and end
    std::atomic<void*> beganFrom{nullptr};    // the begin's caller
};

constexpr std::size_t kShards = 64;
struct Shard {
    std::mutex mutex;
    std::unordered_map<VkCommandBuffer, std::unique_ptr<State>> states;
};
std::array<Shard, kShards> g_shards;

State& stateOf(VkCommandBuffer cb) {
    Shard& shard = g_shards[(reinterpret_cast<std::uintptr_t>(cb) >> 4) % kShards];
    std::lock_guard lock(shard.mutex);
    std::unique_ptr<State>& s = shard.states[cb];
    if (!s) {
        s = std::make_unique<State>();
    }
    return *s;
}

// ---- Reports ----

enum Kind : int { kOverlap, kHandoff, kBeginRecording, kSubmitRecording, kSubmitInCall, kKinds };
constexpr const char* kKindNames[kKinds] = {"two threads in calls on one buffer",
                                            "recording moved to another thread", "begin while recording",
                                            "submit while recording", "submit while a thread records"};
constexpr int kLinesPerKind = 25;
constexpr int kHandoffLines = 0; // one thread begins every context, job threads record: normal
std::array<std::atomic<std::uint64_t>, kKinds> g_counts{};

// A context's current command buffer is at [[context + 0x118]].
constexpr std::uint32_t kContextTable = 0x667F018;
constexpr std::size_t kContextBuffer = 0x118;

} // namespace

// Read under SEH: the pointers are the engine's.
int findContext(VkCommandBuffer cb) {
    __try {
        const auto* table = reinterpret_cast<const std::uintptr_t*>(gameBase() + kContextTable);
        for (int i = 0; i < kCategories * kSlots; ++i) {
            if (!table[i]) {
                continue;
            }
            const auto holder = *reinterpret_cast<const std::uintptr_t*>(table[i] + kContextBuffer);
            if (holder && *reinterpret_cast<const VkCommandBuffer*>(holder) == cb) {
                return i;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -2;
    }
    return -1;
}

namespace {

void report(
    Kind kind, VkCommandBuffer cb, const char* call, void* from, std::uint32_t otherThread, void* otherFrom) {
    const std::uint64_t n = g_counts[kind].fetch_add(1, std::memory_order_relaxed) + 1;
    if (n > (kind == kHandoff ? kHandoffLines : kLinesPerKind)) {
        return;
    }
    char here[32];
    char there[32];
    describeAddress(from, here, sizeof(here));
    describeAddress(otherFrom, there, sizeof(there));
    const int context = findContext(cb);
    char where[48];
    if (context >= 0) {
        std::snprintf(where, sizeof(where), "category %d slot %d", context / kSlots, context % kSlots);
    } else {
        std::snprintf(where, sizeof(where), "%s",
                      context == -1 ? "no context's current buffer" : "unreadable");
    }
    // This thread's game frames (the hooks are ordinary functions, so the unwind reaches the game's).
    void* frames[24] = {};
    const USHORT captured = RtlCaptureStackBackTrace(1, 24, frames, nullptr);
    std::string chain;
    int shown = 0;
    for (USHORT i = 0; i < captured && shown < 14; ++i) {
        char word[32];
        describeAddress(frames[i], word, sizeof(word));
        if (word[0] == 'G') {
            chain += ' ';
            chain += word;
            ++shown;
        }
    }
    EVR_LOG("cb-check: %s (#%llu): %s on buffer %p (%s) from %s on thread %lu; other thread %lu at %s; this "
            "thread's game frames:%s",
            kKindNames[kind], static_cast<unsigned long long>(n), call, static_cast<void*>(cb), where, here,
            GetCurrentThreadId(), static_cast<unsigned long>(otherThread), there, chain.c_str());
}

// ---- The guard around each call ----

class Guard {
public:
    Guard(VkCommandBuffer cb, const char* call, void* from, bool records) : state_(stateOf(cb)) {
        const std::uint32_t self = GetCurrentThreadId();
        std::uint32_t expected = 0;
        if (state_.inCall.compare_exchange_strong(expected, self, std::memory_order_acq_rel)) {
            state_.inCallFrom.store(from, std::memory_order_relaxed);
            owned_ = true;
        } else if (expected != self) {
            report(kOverlap, cb, call, from, expected, state_.inCallFrom.load(std::memory_order_relaxed));
        }
        if (records) {
            const std::uint32_t before = state_.recorder.exchange(self, std::memory_order_acq_rel);
            void* beforeFrom = state_.recorderFrom.exchange(from, std::memory_order_relaxed);
            if (before && before != self && state_.recording.load(std::memory_order_acquire)) {
                report(kHandoff, cb, call, from, before, beforeFrom);
            }
        }
    }
    ~Guard() {
        if (owned_) {
            state_.inCall.store(0, std::memory_order_release);
        }
    }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    State& state() { return state_; }

private:
    State& state_;
    bool owned_ = false;
};

// ---- Hooks ----

VKAPI_ATTR VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer cb, const VkCommandBufferBeginInfo* info) {
    void* from = _ReturnAddress();
    Guard guard(cb, "vkBeginCommandBuffer", from, false);
    State& s = guard.state();
    if (s.recording.exchange(true, std::memory_order_acq_rel)) {
        report(kBeginRecording, cb, "vkBeginCommandBuffer", from, 0,
               s.beganFrom.load(std::memory_order_relaxed));
    }
    s.beganFrom.store(from, std::memory_order_relaxed);
    s.recorder.store(GetCurrentThreadId(), std::memory_order_release);
    s.recorderFrom.store(from, std::memory_order_relaxed);
    return g_next.beginCommandBuffer(cb, info);
}

VKAPI_ATTR VkResult VKAPI_CALL EndCommandBuffer(VkCommandBuffer cb) {
    Guard guard(cb, "vkEndCommandBuffer", _ReturnAddress(), true);
    const VkResult result = g_next.endCommandBuffer(cb);
    guard.state().recording.store(false, std::memory_order_release);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL ResetCommandBuffer(VkCommandBuffer cb, VkCommandBufferResetFlags flags) {
    Guard guard(cb, "vkResetCommandBuffer", _ReturnAddress(), false);
    guard.state().recording.store(false, std::memory_order_release);
    return g_next.resetCommandBuffer(cb, flags);
}

VKAPI_ATTR void VKAPI_CALL CmdBeginRenderPass(VkCommandBuffer cb,
                                              const VkRenderPassBeginInfo* info,
                                              VkSubpassContents contents) {
    Guard guard(cb, "vkCmdBeginRenderPass", _ReturnAddress(), true);
    g_next.cmdBeginRenderPass(cb, info, contents);
}

VKAPI_ATTR void VKAPI_CALL CmdEndRenderPass(VkCommandBuffer cb) {
    Guard guard(cb, "vkCmdEndRenderPass", _ReturnAddress(), true);
    g_next.cmdEndRenderPass(cb);
}

VKAPI_ATTR void VKAPI_CALL CmdBindPipeline(VkCommandBuffer cb,
                                           VkPipelineBindPoint point,
                                           VkPipeline pipeline) {
    Guard guard(cb, "vkCmdBindPipeline", _ReturnAddress(), true);
    g_next.cmdBindPipeline(cb, point, pipeline);
}

VKAPI_ATTR void VKAPI_CALL CmdBindDescriptorSets(VkCommandBuffer cb,
                                                 VkPipelineBindPoint point,
                                                 VkPipelineLayout layout,
                                                 std::uint32_t first,
                                                 std::uint32_t count,
                                                 const VkDescriptorSet* sets,
                                                 std::uint32_t dynamicCount,
                                                 const std::uint32_t* dynamic) {
    Guard guard(cb, "vkCmdBindDescriptorSets", _ReturnAddress(), true);
    g_next.cmdBindDescriptorSets(cb, point, layout, first, count, sets, dynamicCount, dynamic);
}

VKAPI_ATTR void VKAPI_CALL CmdDraw(VkCommandBuffer cb,
                                   std::uint32_t vertices,
                                   std::uint32_t instances,
                                   std::uint32_t firstVertex,
                                   std::uint32_t firstInstance) {
    Guard guard(cb, "vkCmdDraw", _ReturnAddress(), true);
    noteCaller(cb, "vkCmdDraw");
    if (!skipDraw(cb)) {
        g_next.cmdDraw(cb, vertices, instances, firstVertex, firstInstance);
    }
}

VKAPI_ATTR void VKAPI_CALL CmdDrawIndexed(VkCommandBuffer cb,
                                          std::uint32_t indices,
                                          std::uint32_t instances,
                                          std::uint32_t firstIndex,
                                          std::int32_t vertexOffset,
                                          std::uint32_t firstInstance) {
    Guard guard(cb, "vkCmdDrawIndexed", _ReturnAddress(), true);
    noteCaller(cb, "vkCmdDrawIndexed");
    if (!skipDraw(cb)) {
        g_next.cmdDrawIndexed(cb, indices, instances, firstIndex, vertexOffset, firstInstance);
    }
}

VKAPI_ATTR void VKAPI_CALL CmdDispatch(VkCommandBuffer cb,
                                       std::uint32_t x,
                                       std::uint32_t y,
                                       std::uint32_t z) {
    Guard guard(cb, "vkCmdDispatch", _ReturnAddress(), true);
    noteCaller(cb, "vkCmdDispatch");
    if (!skipDraw(cb)) {
        g_next.cmdDispatch(cb, x, y, z);
    }
}

VKAPI_ATTR void VKAPI_CALL CmdDrawIndirect(
    VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, std::uint32_t count, std::uint32_t stride) {
    Guard guard(cb, "vkCmdDrawIndirect", _ReturnAddress(), true);
    noteCaller(cb, "vkCmdDrawIndirect");
    if (!skipDraw(cb)) {
        g_next.cmdDrawIndirect(cb, buffer, offset, count, stride);
    }
}

VKAPI_ATTR void VKAPI_CALL CmdDrawIndexedIndirect(
    VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, std::uint32_t count, std::uint32_t stride) {
    Guard guard(cb, "vkCmdDrawIndexedIndirect", _ReturnAddress(), true);
    noteCaller(cb, "vkCmdDrawIndexedIndirect");
    if (!skipDraw(cb)) {
        g_next.cmdDrawIndexedIndirect(cb, buffer, offset, count, stride);
    }
}

VKAPI_ATTR void VKAPI_CALL CmdDispatchIndirect(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset) {
    Guard guard(cb, "vkCmdDispatchIndirect", _ReturnAddress(), true);
    noteCaller(cb, "vkCmdDispatchIndirect");
    if (!skipDraw(cb)) {
        g_next.cmdDispatchIndirect(cb, buffer, offset);
    }
}

VKAPI_ATTR void VKAPI_CALL CmdPipelineBarrier(VkCommandBuffer cb,
                                              VkPipelineStageFlags src,
                                              VkPipelineStageFlags dst,
                                              VkDependencyFlags flags,
                                              std::uint32_t memoryCount,
                                              const VkMemoryBarrier* memory,
                                              std::uint32_t bufferCount,
                                              const VkBufferMemoryBarrier* buffers,
                                              std::uint32_t imageCount,
                                              const VkImageMemoryBarrier* images) {
    Guard guard(cb, "vkCmdPipelineBarrier", _ReturnAddress(), true);
    g_next.cmdPipelineBarrier(cb, src, dst, flags, memoryCount, memory, bufferCount, buffers, imageCount,
                              images);
}

VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit(VkQueue queue,
                                           std::uint32_t count,
                                           const VkSubmitInfo* submits,
                                           VkFence fence) {
    void* from = _ReturnAddress();
    for (std::uint32_t i = 0; i < count; ++i) {
        for (std::uint32_t j = 0; j < submits[i].commandBufferCount; ++j) {
            const VkCommandBuffer cb = submits[i].pCommandBuffers[j];
            State& s = stateOf(cb);
            if (s.recording.load(std::memory_order_acquire)) {
                report(kSubmitRecording, cb, "vkQueueSubmit", from,
                       s.recorder.load(std::memory_order_relaxed),
                       s.recorderFrom.load(std::memory_order_relaxed));
            }
            if (const std::uint32_t t = s.inCall.load(std::memory_order_acquire)) {
                report(kSubmitInCall, cb, "vkQueueSubmit", from, t,
                       s.inCallFrom.load(std::memory_order_relaxed));
            }
        }
    }
    return g_next.queueSubmit(queue, count, submits, fence);
}

} // namespace

void onDeviceCreated(DeviceData& data, bool isGame, LayerHookFn layerHook) {
    if (!isGame || !viewSlotsActive() || g_on.load() || !readEnabled()) {
        return;
    }
    Next& n = g_next;
    n.key = keyOf(data.device);
    const auto chained = [&data, layerHook](const char* name) {
        const PFN_vkVoidFunction hook = layerHook(name);
        return hook ? hook : data.nextGetDeviceProcAddr(data.device, name);
    };
#define EVR_CHAIN(member, name) n.member = reinterpret_cast<PFN_vk##name>(chained("vk" #name));
    EVR_CHAIN(beginCommandBuffer, BeginCommandBuffer)
    EVR_CHAIN(endCommandBuffer, EndCommandBuffer)
    EVR_CHAIN(resetCommandBuffer, ResetCommandBuffer)
    EVR_CHAIN(cmdBeginRenderPass, CmdBeginRenderPass)
    EVR_CHAIN(cmdEndRenderPass, CmdEndRenderPass)
    EVR_CHAIN(cmdBindPipeline, CmdBindPipeline)
    EVR_CHAIN(cmdBindDescriptorSets, CmdBindDescriptorSets)
    EVR_CHAIN(cmdDraw, CmdDraw)
    EVR_CHAIN(cmdDrawIndexed, CmdDrawIndexed)
    EVR_CHAIN(cmdDispatch, CmdDispatch)
    EVR_CHAIN(cmdDrawIndirect, CmdDrawIndirect)
    EVR_CHAIN(cmdDrawIndexedIndirect, CmdDrawIndexedIndirect)
    EVR_CHAIN(cmdDispatchIndirect, CmdDispatchIndirect)
    EVR_CHAIN(cmdPipelineBarrier, CmdPipelineBarrier)
    EVR_CHAIN(queueSubmit, QueueSubmit)
#undef EVR_CHAIN
    g_on.store(true);
    EVR_LOG("cb-check: on (ETERNALVR_TEST_CB_CHECK: command buffer use of the game's device is checked)");
}

void onDeviceDestroyed(VkDevice device) {
    if (g_on.load() && keyOf(device) == g_next.key) {
        g_on.store(false);
    }
}

PFN_vkVoidFunction findHook(VkDevice device, const char* name) {
    if (!g_on.load(std::memory_order_acquire) || !device || keyOf(device) != g_next.key) {
        return nullptr; // only the game's device, once its functions are chained (they call its next layer)
    }
#define EVR_HOOK(fn)                                                                                         \
    if (std::strcmp(name, "vk" #fn) == 0) {                                                                  \
        return reinterpret_cast<PFN_vkVoidFunction>(&fn);                                                    \
    }
    EVR_HOOK(BeginCommandBuffer)
    EVR_HOOK(EndCommandBuffer)
    EVR_HOOK(ResetCommandBuffer)
    EVR_HOOK(CmdBeginRenderPass)
    EVR_HOOK(CmdEndRenderPass)
    EVR_HOOK(CmdBindPipeline)
    EVR_HOOK(CmdBindDescriptorSets)
    EVR_HOOK(CmdDraw)
    EVR_HOOK(CmdDrawIndexed)
    EVR_HOOK(CmdDispatch)
    EVR_HOOK(CmdDrawIndirect)
    EVR_HOOK(CmdDrawIndexedIndirect)
    EVR_HOOK(CmdDispatchIndirect)
    EVR_HOOK(CmdPipelineBarrier)
    EVR_HOOK(QueueSubmit)
#undef EVR_HOOK
    return nullptr;
}

} // namespace evr::vkcore::cb_check
