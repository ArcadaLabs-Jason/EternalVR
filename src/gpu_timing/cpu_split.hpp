#pragma once

// The CPU split of a stereo tick (ETERNALVR_CPU_TIMING=1, docs/VR_STEREO.md "CPU timing"): where the CPU
// time of one tick goes, per stage, and which threads are busy. The layer measures (vkcore/cpu_timing);
// this is the arithmetic and the summaries, tested on every platform.

#include "gpu_timing/stats.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace evr::gpu_timing {

// The measured stages. The frontend ones run on the thread that ends the engine's render frames (the
// frame-end job the stereo hooks wrap); the call ones on whichever thread makes the Vulkan call.
enum class CpuStage : std::uint8_t {
    FrameEndLeft,  // eye L's frame-end job (it hands eye L to the render thread)
    RightEye,      // eye R's render on the frontend thread: its views and its own frame-end job
    Drain,         // holding the frontend until the render thread is idle (a new eye tag base)
    Submit,        // vkQueueSubmit calls
    FenceWait,     // vkWaitForFences calls
    SemaphoreWait, // vkWaitSemaphores calls
    Present,       // vkQueuePresentKHR calls (the XR presenter's work included)
};
inline constexpr std::size_t kCpuStages = 7;

const char* cpuStageName(CpuStage stage);

// One stereo tick as the frontend thread saw it: from the start of one eye L frame-end job to the next.
struct CpuTick {
    double periodMs = 0.0; // wall time of the tick on the frontend thread
    // CPU time the frontend thread used in it; negative when the tick's two frame-end jobs ran on different
    // threads (the engine's job system picks any worker), which leaves it unknown.
    double frontendCpuMs = -1.0;
    double processCpuMs = 0.0; // CPU time of every thread of the process in the same stretch
    std::array<double, kCpuStages> wallMs{};
    std::array<double, kCpuStages> cpuMs{};
    std::array<std::uint32_t, kCpuStages> calls{};
};

class CpuSplitWindow {
public:
    void add(const CpuTick& tick);

    struct Report {
        std::uint64_t ticks = 0;
        Summary period;
        Summary frontendCpu; // the ticks whose frontend CPU time is known
        Summary processCpu;
        // The frontend's time outside the wrapped frame-end jobs: the game's own frame (simulation) and
        // eye L's views, plus anything the frontend waits on there.
        Summary outsideWall;
        Summary outsideCpu;
        std::array<Summary, kCpuStages> wall{};
        std::array<Summary, kCpuStages> cpu{};
        std::array<double, kCpuStages> callsPerTick{};
    };
    [[nodiscard]] Report report() const;

    void clear();

private:
    std::vector<double> period_;
    std::vector<double> frontendCpu_;
    std::vector<double> processCpu_;
    std::vector<double> outsideWall_;
    std::vector<double> outsideCpu_;
    std::array<std::vector<double>, kCpuStages> wall_;
    std::array<std::vector<double>, kCpuStages> cpu_;
    std::array<std::uint64_t, kCpuStages> calls_{};
};

// A thread's CPU time counter (QueryThreadCycleTime) at one moment.
struct ThreadCycles {
    std::uint32_t id = 0;
    std::uint64_t cycles = 0;
    std::string name; // the thread's description, or empty
};

struct ThreadShare {
    std::uint32_t id = 0;
    std::string name;
    double msPerTick = 0.0;   // CPU time per stereo tick
    double busyPercent = 0.0; // of one core over the window
};

// The threads that used the most CPU between two snapshots, busiest first (at most `top`); a thread that
// is new in `after` counts from zero. `total` gets the whole process's CPU time per tick (every thread).
std::vector<ThreadShare> rankThreads(const std::vector<ThreadCycles>& before,
                                     const std::vector<ThreadCycles>& after,
                                     double cyclesPerMs,
                                     double windowMs,
                                     std::uint64_t ticks,
                                     std::size_t top,
                                     double& total);

// "RenderThread 7.10 ms (58%)"; the id stands in for a missing name.
std::string formatShare(const ThreadShare& s);

} // namespace evr::gpu_timing
