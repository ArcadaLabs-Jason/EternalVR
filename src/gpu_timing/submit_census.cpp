#include "gpu_timing/submit_census.hpp"

#include <algorithm>
#include <cstdio>

namespace evr::gpu_timing {

void SubmitCensus::add(const SubmitRecord& r) {
    Report& c = counts_;
    ++c.calls;
    c.batches += r.batches;
    c.commandBuffers += r.commandBuffers;
    c.waits += r.waits;
    c.signals += r.signals;
    c.fences += r.fence ? 1 : 0;
    c.emptyCalls += r.batches == 0 ? 1 : 0;
    ++perQueue_[r.queue];
    threads_.insert(r.thread);
    const auto last = lastAtMs_.find(r.queue);
    if (last != lastAtMs_.end()) {
        const double gap = std::max(0.0, r.atMs - last->second);
        std::size_t bucket = 0;
        while (bucket < kSubmitGapLimitsMs.size() && gap >= kSubmitGapLimitsMs[bucket]) {
            ++bucket;
        }
        ++c.gaps[bucket];
        c.mergeable += gap < kSubmitMergeGapMs ? 1 : 0;
    }
    lastAtMs_[r.queue] = r.atMs;
}

SubmitCensus::Report SubmitCensus::report() const {
    Report r = counts_;
    r.queues = perQueue_.size();
    r.threads = threads_.size();
    for (const auto& [queue, calls] : perQueue_) {
        r.busiestQueueCalls = std::max(r.busiestQueueCalls, calls);
    }
    return r;
}

void SubmitCensus::clear() {
    counts_ = Report{};
    perQueue_.clear();
    threads_.clear();
}

std::string formatSubmitCensus(const SubmitCensus::Report& r, std::uint64_t ticks) {
    const double n = ticks ? static_cast<double>(ticks) : 1.0;
    const auto per = [&](std::uint64_t v) {
        return static_cast<double>(v) / n;
    };
    char text[400];
    std::snprintf(
        text, sizeof(text),
        "%.1f call(s), %.1f batch(es), %.1f command buffer(s), %.1f wait / %.1f signal semaphore(s), "
        "%.1f fence(s), %.1f empty call(s) per %s; %zu queue(s) (busiest %.1f call(s)), %zu thread(s); "
        "gap to the previous submit on the queue <0.05 / <0.2 / <1 / >=1 ms: %.1f / %.1f / %.1f / %.1f; "
        "mergeable (gap < %.1f ms) %.1f",
        per(r.calls), per(r.batches), per(r.commandBuffers), per(r.waits), per(r.signals), per(r.fences),
        per(r.emptyCalls), ticks ? "tick" : "window", r.queues, per(r.busiestQueueCalls), r.threads,
        per(r.gaps[0]), per(r.gaps[1]), per(r.gaps[2]), per(r.gaps[3]), kSubmitMergeGapMs, per(r.mergeable));
    return text;
}

} // namespace evr::gpu_timing
