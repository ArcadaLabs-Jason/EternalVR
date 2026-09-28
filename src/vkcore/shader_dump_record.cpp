// Shader and draw dump: command recording and the frame window (see shader_dump.hpp).
//
// Each line of drawlog.jsonl is one recorded command, tagged with the frame ("f": the number of game
// presents before it was recorded) and the command buffer ("cb"). Draws and dispatches carry the pipeline
// bound at that point of their command buffer, so the census needs no replay of binds for its counts.
// Outside the window every hook is a pass-through after one atomic load.

#include "vkcore/log.hpp"
#include "vkcore/shader_dump.hpp"
#include "vkcore/shader_dump_impl.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace evr::vkcore::shader_dump {

namespace {

using dump::handleText;
using dump::handleValue;

// Push-constant bytes logged per update (Vulkan guarantees 128; the game's ranges are smaller).
constexpr std::uint32_t kMaxPushBytes = 256;

struct CmdState {
    VkPipeline graphics = VK_NULL_HANDLE;
    VkPipeline compute = VK_NULL_HANDLE;
};

std::atomic<bool> g_recording{false};
std::atomic<std::uint64_t> g_frame{0};
std::atomic<bool> g_windowDone{false};
dump::FrameWindow g_window{0, 0};

std::mutex& g_mutex = *new std::mutex;
auto& g_cmd = *new std::unordered_map<VkCommandBuffer, CmdState>;
std::string& g_pending = *new std::string;

// The line prefix {"f":N,"cb":"0x..","e":"<event>"
std::string head(VkCommandBuffer cb, const char* event) {
    return "{\"f\":" + std::to_string(g_frame.load(std::memory_order_relaxed)) + ",\"cb\":\"" +
           handleText(handleValue(cb)) + "\",\"e\":\"" + event + "\"";
}

void emit(std::string line) {
    line += "}\n";
    std::lock_guard lock(g_mutex);
    g_pending += line;
}

bool logging(DumpDevice* d) {
    return g_recording.load(std::memory_order_relaxed) && d->isGame;
}

VkPipeline boundPipeline(VkCommandBuffer cb, bool compute) {
    std::lock_guard lock(g_mutex);
    const auto it = g_cmd.find(cb);
    if (it == g_cmd.end()) {
        return VK_NULL_HANDLE;
    }
    return compute ? it->second.compute : it->second.graphics;
}

void logDraw(VkCommandBuffer cb, const char* event, std::string fields) {
    const VkPipeline p = boundPipeline(cb, false);
    emit(head(cb, event) + ",\"p\":\"" + handleText(handleValue(p)) + "\"" + fields);
}

void logDispatch(VkCommandBuffer cb, const char* event, std::string fields) {
    const VkPipeline p = boundPipeline(cb, true);
    emit(head(cb, event) + ",\"p\":\"" + handleText(handleValue(p)) + "\"" + fields);
}

VKAPI_ATTR VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer cb, const VkCommandBufferBeginInfo* pInfo) {
    DumpDevice* d = deviceOf(cb);
    if (logging(d)) {
        {
            std::lock_guard lock(g_mutex);
            g_cmd[cb] = CmdState{};
        }
        emit(head(cb, "begin") + ",\"flags\":" + std::to_string(pInfo->flags) +
             ",\"secondary\":" + (pInfo->pInheritanceInfo ? "true" : "false"));
    }
    return d->BeginCommandBuffer(cb, pInfo);
}

VKAPI_ATTR void VKAPI_CALL CmdBindPipeline(VkCommandBuffer cb,
                                           VkPipelineBindPoint bindPoint,
                                           VkPipeline pipeline) {
    DumpDevice* d = deviceOf(cb);
    if (logging(d)) {
        {
            std::lock_guard lock(g_mutex);
            CmdState& s = g_cmd[cb];
            if (bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
                s.compute = pipeline;
            } else if (bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
                s.graphics = pipeline;
            }
        }
        emit(head(cb, "pipe") + ",\"bp\":" + std::to_string(bindPoint) + ",\"p\":\"" +
             handleText(handleValue(pipeline)) + "\"");
    }
    d->CmdBindPipeline(cb, bindPoint, pipeline);
}

VKAPI_ATTR void VKAPI_CALL CmdBindDescriptorSets(VkCommandBuffer cb,
                                                 VkPipelineBindPoint bindPoint,
                                                 VkPipelineLayout layout,
                                                 std::uint32_t firstSet,
                                                 std::uint32_t setCount,
                                                 const VkDescriptorSet* pSets,
                                                 std::uint32_t dynamicCount,
                                                 const std::uint32_t* pDynamicOffsets) {
    DumpDevice* d = deviceOf(cb);
    if (logging(d)) {
        std::string line = head(cb, "sets") + ",\"bp\":" + std::to_string(bindPoint) + ",\"layout\":\"" +
                           handleText(handleValue(layout)) + "\",\"first\":" + std::to_string(firstSet) +
                           ",\"sets\":[";
        for (std::uint32_t i = 0; i < setCount; ++i) {
            line += (i ? ",\"" : "\"") + handleText(handleValue(pSets[i])) + "\"";
        }
        line += "],\"dyn\":[";
        for (std::uint32_t i = 0; i < dynamicCount; ++i) {
            line += (i ? "," : "") + std::to_string(pDynamicOffsets[i]);
        }
        emit(line + "]");
    }
    d->CmdBindDescriptorSets(cb, bindPoint, layout, firstSet, setCount, pSets, dynamicCount, pDynamicOffsets);
}

VKAPI_ATTR void VKAPI_CALL CmdPushConstants(VkCommandBuffer cb,
                                            VkPipelineLayout layout,
                                            VkShaderStageFlags stages,
                                            std::uint32_t offset,
                                            std::uint32_t size,
                                            const void* pValues) {
    DumpDevice* d = deviceOf(cb);
    if (logging(d)) {
        emit(head(cb, "push") + ",\"layout\":\"" + handleText(handleValue(layout)) +
             "\",\"stages\":" + std::to_string(stages) + ",\"off\":" + std::to_string(offset) +
             ",\"size\":" + std::to_string(size) + ",\"data\":\"" +
             dump::hexBytes(pValues, std::min(size, kMaxPushBytes)) + "\"");
    }
    d->CmdPushConstants(cb, layout, stages, offset, size, pValues);
}

VKAPI_ATTR void VKAPI_CALL CmdDraw(VkCommandBuffer cb,
                                   std::uint32_t vertexCount,
                                   std::uint32_t instanceCount,
                                   std::uint32_t firstVertex,
                                   std::uint32_t firstInstance) {
    DumpDevice* d = deviceOf(cb);
    if (logging(d)) {
        logDraw(cb, "draw",
                ",\"n\":" + std::to_string(vertexCount) + ",\"inst\":" + std::to_string(instanceCount));
    }
    d->CmdDraw(cb, vertexCount, instanceCount, firstVertex, firstInstance);
}

VKAPI_ATTR void VKAPI_CALL CmdDrawIndexed(VkCommandBuffer cb,
                                          std::uint32_t indexCount,
                                          std::uint32_t instanceCount,
                                          std::uint32_t firstIndex,
                                          std::int32_t vertexOffset,
                                          std::uint32_t firstInstance) {
    DumpDevice* d = deviceOf(cb);
    if (logging(d)) {
        logDraw(cb, "drawIndexed",
                ",\"n\":" + std::to_string(indexCount) + ",\"inst\":" + std::to_string(instanceCount));
    }
    d->CmdDrawIndexed(cb, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

VKAPI_ATTR void VKAPI_CALL CmdDrawIndirect(
    VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, std::uint32_t count, std::uint32_t stride) {
    DumpDevice* d = deviceOf(cb);
    if (logging(d)) {
        logDraw(cb, "drawIndirect", ",\"count\":" + std::to_string(count));
    }
    d->CmdDrawIndirect(cb, buffer, offset, count, stride);
}

VKAPI_ATTR void VKAPI_CALL CmdDrawIndexedIndirect(
    VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, std::uint32_t count, std::uint32_t stride) {
    DumpDevice* d = deviceOf(cb);
    if (logging(d)) {
        logDraw(cb, "drawIndexedIndirect", ",\"count\":" + std::to_string(count));
    }
    d->CmdDrawIndexedIndirect(cb, buffer, offset, count, stride);
}

VKAPI_ATTR void VKAPI_CALL CmdDrawIndirectCount(VkCommandBuffer cb,
                                                VkBuffer buffer,
                                                VkDeviceSize offset,
                                                VkBuffer countBuffer,
                                                VkDeviceSize countOffset,
                                                std::uint32_t maxCount,
                                                std::uint32_t stride) {
    DumpDevice* d = deviceOf(cb);
    if (logging(d)) {
        logDraw(cb, "drawIndirectCount", ",\"max\":" + std::to_string(maxCount));
    }
    d->CmdDrawIndirectCount(cb, buffer, offset, countBuffer, countOffset, maxCount, stride);
}

VKAPI_ATTR void VKAPI_CALL CmdDrawIndexedIndirectCount(VkCommandBuffer cb,
                                                       VkBuffer buffer,
                                                       VkDeviceSize offset,
                                                       VkBuffer countBuffer,
                                                       VkDeviceSize countOffset,
                                                       std::uint32_t maxCount,
                                                       std::uint32_t stride) {
    DumpDevice* d = deviceOf(cb);
    if (logging(d)) {
        logDraw(cb, "drawIndexedIndirectCount", ",\"max\":" + std::to_string(maxCount));
    }
    d->CmdDrawIndexedIndirectCount(cb, buffer, offset, countBuffer, countOffset, maxCount, stride);
}

VKAPI_ATTR void VKAPI_CALL CmdDispatch(VkCommandBuffer cb,
                                       std::uint32_t x,
                                       std::uint32_t y,
                                       std::uint32_t z) {
    DumpDevice* d = deviceOf(cb);
    if (logging(d)) {
        logDispatch(cb, "dispatch",
                    ",\"g\":[" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + "]");
    }
    d->CmdDispatch(cb, x, y, z);
}

VKAPI_ATTR void VKAPI_CALL CmdDispatchIndirect(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset) {
    DumpDevice* d = deviceOf(cb);
    if (logging(d)) {
        logDispatch(cb, "dispatchIndirect", "");
    }
    d->CmdDispatchIndirect(cb, buffer, offset);
}

VKAPI_ATTR void VKAPI_CALL CmdExecuteCommands(VkCommandBuffer cb,
                                              std::uint32_t count,
                                              const VkCommandBuffer* pBuffers) {
    DumpDevice* d = deviceOf(cb);
    if (logging(d)) {
        std::string line = head(cb, "exec") + ",\"cbs\":[";
        for (std::uint32_t i = 0; i < count; ++i) {
            line += (i ? ",\"" : "\"") + handleText(handleValue(pBuffers[i])) + "\"";
        }
        emit(line + "]");
    }
    d->CmdExecuteCommands(cb, count, pBuffers);
}

void flush() {
    std::string lines;
    {
        std::lock_guard lock(g_mutex);
        lines.swap(g_pending);
    }
    write(Stream::DrawLog, std::move(lines));
}

} // namespace

PFN_vkVoidFunction findRecordHook(const char* name) {
#define EVR_DUMP_HOOK_AS(fn, text)                                                                           \
    if (std::strcmp(name, text) == 0) {                                                                      \
        return reinterpret_cast<PFN_vkVoidFunction>(&fn);                                                    \
    }
#define EVR_DUMP_HOOK(fn) EVR_DUMP_HOOK_AS(fn, "vk" #fn)
    EVR_DUMP_HOOK(BeginCommandBuffer)
    EVR_DUMP_HOOK(CmdBindPipeline)
    EVR_DUMP_HOOK(CmdBindDescriptorSets)
    EVR_DUMP_HOOK(CmdPushConstants)
    EVR_DUMP_HOOK(CmdDraw)
    EVR_DUMP_HOOK(CmdDrawIndexed)
    EVR_DUMP_HOOK(CmdDrawIndirect)
    EVR_DUMP_HOOK(CmdDrawIndexedIndirect)
    EVR_DUMP_HOOK(CmdDrawIndirectCount)
    EVR_DUMP_HOOK_AS(CmdDrawIndirectCount, "vkCmdDrawIndirectCountKHR")
    EVR_DUMP_HOOK(CmdDrawIndexedIndirectCount)
    EVR_DUMP_HOOK_AS(CmdDrawIndexedIndirectCount, "vkCmdDrawIndexedIndirectCountKHR")
    EVR_DUMP_HOOK(CmdDispatch)
    EVR_DUMP_HOOK(CmdDispatchIndirect)
    EVR_DUMP_HOOK(CmdExecuteCommands)
#undef EVR_DUMP_HOOK
#undef EVR_DUMP_HOOK_AS
    return nullptr;
}

bool recording() {
    return g_recording.load(std::memory_order_relaxed);
}

std::uint64_t currentFrame() {
    return g_frame.load(std::memory_order_relaxed);
}

void startRecording(std::uint32_t skip, std::uint32_t count) {
    g_window = dump::FrameWindow(skip, count);
    g_recording.store(g_window.contains(0));
}

void onPresent(VkDevice device) {
    if (!enabled() || g_windowDone.load(std::memory_order_relaxed)) {
        return;
    }
    DumpDevice* d = deviceOf(device);
    if (!d || !d->isGame) {
        return;
    }
    const std::uint64_t frame = g_frame.fetch_add(1) + 1;
    g_recording.store(g_window.contains(frame));
    flush();
    if (g_window.finished(frame)) {
        g_windowDone.store(true);
        {
            std::lock_guard lock(g_mutex);
            g_cmd.clear();
        }
        flush();
        EVR_LOG("shader dump: draw log complete after %llu presents", static_cast<unsigned long long>(frame));
    }
}

} // namespace evr::vkcore::shader_dump
