#pragma once

// CPU timing (ETERNALVR_CPU_TIMING=1, docs/VR_STEREO.md "CPU timing"): where the CPU time of a stereo
// tick goes. A tick runs from the start of one frame-end job on the frontend thread to the next (the
// stereo hooks mark it). Within it the frontend's wrapped stages (eye L's frame end, eye R's render, a
// drain) and the Vulkan calls that can take CPU time or block (vkQueueSubmit, vkWaitForFences,
// vkWaitSemaphores, vkQueuePresentKHR) are timed on the wall clock and on the calling thread's CPU time
// (QueryThreadCycleTime). Every 10 s the log gets `cpu:` lines with the split per tick and the busiest
// threads of the process. Off (no hook handed out, every entry point returns at once) unless
// ETERNALVR_CPU_TIMING=1.

#include "gpu_timing/cpu_split.hpp"
#include "vkcore/dispatch.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace evr::vkcore::cpu_timing {

using Stage = ::evr::gpu_timing::CpuStage;

// ETERNALVR_CPU_TIMING=1 (read once).
bool enabled();

void onDeviceCreated(DeviceData& data, bool isGame);
void onDeviceDestroyed(VkDevice device);

// The layer's hook for `name` (vkQueueSubmit, vkWaitForFences, vkWaitSemaphores[KHR]), or nullptr; always
// nullptr when CPU timing is off. Each chains to the GPU timing, UI layer or shader dump hook of the same
// function where one is handed out, else to the next layer.
PFN_vkVoidFunction findHook(const char* name);

// The frontend thread starts a frame-end job that is not eye R's own: the previous tick ends here (every
// 10 s the finished window goes to a reporting thread of its own, which logs the summary).
void onFrontendTick();

// Times a stage on the calling thread from construction to destruction (wall clock and the thread's CPU
// time).
class Scope {
public:
    explicit Scope(Stage stage);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Stage stage_;
    bool on_;
    std::int64_t startTicks_ = 0;
    std::uint64_t startCycles_ = 0;
};

} // namespace evr::vkcore::cpu_timing
