#pragma once

// The vkQueueSubmit census of CPU timing (ETERNALVR_CPU_TIMING=1, docs/VR_STEREO.md "CPU timing"): how the
// game's submits are shaped (batches, command buffers, semaphores, fences, queues, threads) and how close
// together they come on each queue. It answers whether holding a submit to merge it with the next one would
// save driver calls without starving the GPU. The layer records (vkcore/cpu_timing); this is the arithmetic.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace evr::gpu_timing {

// One vkQueueSubmit call.
struct SubmitRecord {
    std::uint64_t queue = 0;
    std::uint32_t thread = 0;
    double atMs = 0.0;                // when the call started
    std::uint32_t batches = 0;        // submitCount
    std::uint32_t commandBuffers = 0; // over all batches
    std::uint32_t waits = 0;          // wait semaphores over all batches
    std::uint32_t signals = 0;        // signal semaphores over all batches
    bool fence = false;
};

// Gap from the previous submit on the same queue: under 0.05 ms, 0.2 ms, 1 ms, and 1 ms or more.
inline constexpr std::size_t kSubmitGapBuckets = 4;
inline constexpr std::array<double, kSubmitGapBuckets - 1> kSubmitGapLimitsMs{0.05, 0.2, 1.0};
// A submit this close to the previous one on its queue could have been merged into it at little GPU cost.
inline constexpr double kSubmitMergeGapMs = 0.2;

class SubmitCensus {
public:
    void add(const SubmitRecord& r);

    struct Report {
        std::uint64_t calls = 0;
        std::uint64_t batches = 0;
        std::uint64_t commandBuffers = 0;
        std::uint64_t waits = 0;
        std::uint64_t signals = 0;
        std::uint64_t fences = 0;
        std::uint64_t emptyCalls = 0; // no batch at all (a fence-only submit)
        std::size_t queues = 0;
        std::size_t threads = 0;
        std::uint64_t busiestQueueCalls = 0;
        // Submits that had a previous submit on the same queue, by gap bucket.
        std::array<std::uint64_t, kSubmitGapBuckets> gaps{};
        std::uint64_t mergeable = 0; // gap under kSubmitMergeGapMs
    };
    [[nodiscard]] Report report() const;

    // Starts a new window; the last submit per queue is kept so that the first gap of the window is known.
    void clear();

private:
    Report counts_;
    std::unordered_map<std::uint64_t, std::uint64_t> perQueue_;
    std::unordered_set<std::uint32_t> threads_;
    std::unordered_map<std::uint64_t, double> lastAtMs_;
};

// "14.0 call(s), 14.2 batch(es), ..." per tick over `ticks` stereo ticks (per window when `ticks` is 0).
std::string formatSubmitCensus(const SubmitCensus::Report& r, std::uint64_t ticks);

} // namespace evr::gpu_timing
