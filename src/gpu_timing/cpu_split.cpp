#include "gpu_timing/cpu_split.hpp"

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <utility>

namespace evr::gpu_timing {

namespace {

// The frontend stages: the wrapped parts of the frontend thread's tick.
constexpr std::array<CpuStage, 3> kFrontendStages{CpuStage::FrameEndLeft, CpuStage::RightEye,
                                                  CpuStage::Drain};

std::size_t index(CpuStage stage) {
    return static_cast<std::size_t>(stage);
}

} // namespace

const char* cpuStageName(CpuStage stage) {
    switch (stage) {
    case CpuStage::FrameEndLeft:
        return "eye L frame end";
    case CpuStage::RightEye:
        return "eye R render";
    case CpuStage::Drain:
        return "drain";
    case CpuStage::Submit:
        return "vkQueueSubmit";
    case CpuStage::FenceWait:
        return "vkWaitForFences";
    case CpuStage::SemaphoreWait:
        return "vkWaitSemaphores";
    case CpuStage::Present:
        return "vkQueuePresentKHR";
    }
    return "?";
}

void CpuSplitWindow::add(const CpuTick& tick) {
    period_.push_back(tick.periodMs);
    processCpu_.push_back(tick.processCpuMs);
    double wrappedWall = 0.0;
    double wrappedCpu = 0.0;
    for (const CpuStage s : kFrontendStages) {
        wrappedWall += tick.wallMs[index(s)];
        wrappedCpu += tick.cpuMs[index(s)];
    }
    outsideWall_.push_back(std::max(0.0, tick.periodMs - wrappedWall));
    if (tick.frontendCpuMs >= 0.0) {
        frontendCpu_.push_back(tick.frontendCpuMs);
        outsideCpu_.push_back(std::max(0.0, tick.frontendCpuMs - wrappedCpu));
    }
    for (std::size_t i = 0; i < kCpuStages; ++i) {
        wall_[i].push_back(tick.wallMs[i]);
        cpu_[i].push_back(tick.cpuMs[i]);
        calls_[i] += tick.calls[i];
    }
}

CpuSplitWindow::Report CpuSplitWindow::report() const {
    Report r;
    r.ticks = period_.size();
    r.period = summarize(period_);
    r.frontendCpu = summarize(frontendCpu_);
    r.processCpu = summarize(processCpu_);
    r.outsideWall = summarize(outsideWall_);
    r.outsideCpu = summarize(outsideCpu_);
    for (std::size_t i = 0; i < kCpuStages; ++i) {
        r.wall[i] = summarize(wall_[i]);
        r.cpu[i] = summarize(cpu_[i]);
        r.callsPerTick[i] = r.ticks ? static_cast<double>(calls_[i]) / static_cast<double>(r.ticks) : 0.0;
    }
    return r;
}

void CpuSplitWindow::clear() {
    *this = CpuSplitWindow{};
}

std::vector<ThreadShare> rankThreads(const std::vector<ThreadCycles>& before,
                                     const std::vector<ThreadCycles>& after,
                                     double cyclesPerMs,
                                     double windowMs,
                                     std::uint64_t ticks,
                                     std::size_t top,
                                     double& total) {
    total = 0.0;
    std::vector<ThreadShare> shares;
    if (cyclesPerMs <= 0.0 || windowMs <= 0.0 || ticks == 0) {
        return shares;
    }
    std::unordered_map<std::uint32_t, std::uint64_t> start;
    for (const ThreadCycles& t : before) {
        start[t.id] = t.cycles;
    }
    for (const ThreadCycles& t : after) {
        const auto it = start.find(t.id);
        const std::uint64_t from = it == start.end() || it->second > t.cycles ? 0 : it->second;
        const double ms = static_cast<double>(t.cycles - from) / cyclesPerMs;
        if (ms <= 0.0) {
            continue;
        }
        ThreadShare s;
        s.id = t.id;
        s.name = t.name;
        s.msPerTick = ms / static_cast<double>(ticks);
        s.busyPercent = 100.0 * ms / windowMs;
        total += s.msPerTick;
        shares.push_back(std::move(s));
    }
    std::sort(shares.begin(), shares.end(),
              [](const ThreadShare& a, const ThreadShare& b) { return a.msPerTick > b.msPerTick; });
    if (shares.size() > top) {
        shares.resize(top);
    }
    return shares;
}

std::string formatShare(const ThreadShare& s) {
    char text[160];
    if (s.name.empty()) {
        std::snprintf(text, sizeof(text), "thread %u %.2f ms (%.0f%%)", s.id, s.msPerTick, s.busyPercent);
    } else {
        std::snprintf(text, sizeof(text), "%s [%u] %.2f ms (%.0f%%)", s.name.c_str(), s.id, s.msPerTick,
                      s.busyPercent);
    }
    return text;
}

} // namespace evr::gpu_timing
