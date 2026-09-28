#pragma once

// Internal state of GPU timing (gpu_timing.hpp), shared by gpu_timing.cpp (devices, the submit hook,
// the per-batch command buffers) and gpu_timing_report.cpp (reading results, the log and the CSV).

#include "gpu_timing/query_ring.hpp"
#include "gpu_timing/timing_window.hpp"
#include "vkcore/dispatch.hpp"

#include <vulkan/vulkan.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace evr::vkcore::gpu_timing {

namespace gt = ::evr::gpu_timing;
using Clock = std::chrono::steady_clock;

// 1024 queries: a frame of the game submits a few dozen batches, and results are read a few frames later.
inline constexpr std::uint32_t kPairs = 512;
// Batches of one frame past this go untimed (a long stretch without a present would fill the ring).
inline constexpr std::size_t kMaxBatchesPerFrame = 160;
// A frame whose results are still not ready this many presents after it closed is given up.
inline constexpr std::uint64_t kGiveUpPresents = 60;

struct Functions {
    PFN_vkQueueSubmit queueSubmit = nullptr; // the next in the chain (the UI layer's hook when it is on)
    PFN_vkCreateQueryPool createQueryPool = nullptr;
    PFN_vkDestroyQueryPool destroyQueryPool = nullptr;
    PFN_vkGetQueryPoolResults getQueryPoolResults = nullptr;
    PFN_vkCreateCommandPool createCommandPool = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer endCommandBuffer = nullptr;
    PFN_vkCmdResetQueryPool cmdResetQueryPool = nullptr;
    PFN_vkCmdWriteTimestamp cmdWriteTimestamp = nullptr;
};

// The bracketing command buffers of one queue family, recorded once per pair on first use.
struct FamilyTimer {
    VkCommandPool pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> begin; // [pair]: resets and writes the pair's first query (top of pipe)
    std::vector<VkCommandBuffer> end;   // [pair]: the second (bottom of pipe)
    VkCommandBuffer resetAll = VK_NULL_HANDLE; // resets the whole pool (the first timed batch only)
};

struct Batch {
    std::uint32_t pair = 0;
    std::uint32_t family = 0;
};

struct Frame {
    std::uint64_t id = 0; // the present that closed it (1 for the first)
    gt::FrameEye eye = gt::FrameEye::Mono;
    double cpuMs = 0.0;
    std::uint32_t untimed = 0; // batches submitted without timestamps
    std::vector<Batch> batches;
};

struct Counters {
    std::uint64_t timedBatches = 0; // read back
    std::uint64_t ringFull = 0;
    std::uint64_t notBracketable = 0; // device group or protected submits
    std::uint64_t noTimestamps = 0;   // queue family with timestampValidBits 0 (or unknown queue)
    std::uint64_t frameCap = 0;       // past kMaxBatchesPerFrame
    std::uint64_t lostFrames = 0;     // results never became available, or implausible
};

struct TimingDevice {
    VkDevice device = VK_NULL_HANDLE;
    DeviceData* data = nullptr;
    Functions fn;
    bool active = false; // the game's device with timestamps: submits are bracketed
    float periodNs = 1.0f;
    std::vector<std::uint32_t> validBits; // per queue family

    // Everything below is guarded by `mutex` (submit threads, the present thread, the XR worker's pose
    // age, the presenter's eye tag).
    std::mutex mutex;
    VkQueryPool pool = VK_NULL_HANDLE;
    bool poolReset = false; // a submit carried the reset of the whole pool (queries start uninitialized)
    gt::QueryRing ring{kPairs};
    // Per query, the value read at its last use. A pair reused before the GPU has run its new command
    // buffers still shows the old, available result: a value equal to the last one read is not ready yet.
    std::vector<std::uint64_t> lastValues = std::vector<std::uint64_t>(kPairs * 2, 0);
    std::unordered_map<std::uint32_t, FamilyTimer> timers;
    std::unordered_map<VkQueue, std::uint32_t> queueFamilies;
    Frame open;               // batches submitted since the last present
    std::deque<Frame> closed; // presented, results not read yet (oldest first)
    std::uint64_t presents = 0;
    Clock::time_point lastPresent{};
    gt::TimingWindow window;
    Counters counters;
    Counters reported; // at the last summary
    std::vector<double> poseAges;
    Clock::time_point lastReport{};
    std::FILE* csv = nullptr;
};

// gpu_timing_report.cpp, under the device's mutex: reads every closed frame but the newest whose results
// are ready (in order, stopping at the first that is not), and logs the summary when 10 s have passed.
void resolveFrames(TimingDevice& d);
void reportIfDue(TimingDevice& d, Clock::time_point now);
// Opens eternalvr-gpu-<pid>.csv in ETERNALVR_LOG_DIR (nothing without it).
void openCsv(TimingDevice& d);
void closeCsv(TimingDevice& d);

} // namespace evr::vkcore::gpu_timing
