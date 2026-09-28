#include "gpu_timing/frame_gpu.hpp"

#include "gpu_timing/timestamps.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace evr::gpu_timing {

namespace {

using Interval = std::pair<std::int64_t, std::int64_t>;

// Total length of the union of `intervals` (sorted in place).
std::int64_t unionLength(std::vector<Interval>& intervals) {
    std::sort(intervals.begin(), intervals.end());
    std::int64_t total = 0;
    bool open = false;
    Interval current{};
    for (const Interval& i : intervals) {
        if (open && i.first <= current.second) {
            current.second = std::max(current.second, i.second);
            continue;
        }
        if (open) {
            total += current.second - current.first;
        }
        current = i;
        open = true;
    }
    if (open) {
        total += current.second - current.first;
    }
    return total;
}

} // namespace

FrameGpu measureFrame(const std::vector<BatchTimes>& batches, float periodNs) {
    FrameGpu out;
    out.batches = static_cast<std::uint32_t>(batches.size());
    if (batches.empty()) {
        return out;
    }
    std::uint32_t bits = 64;
    for (const BatchTimes& b : batches) {
        bits = std::min(bits, b.validBits);
    }
    if (bits == 0) {
        return out;
    }
    const std::uint64_t reference = batches.front().begin;
    std::vector<Interval> all;
    std::array<std::vector<Interval>, kFamilies> perFamily;
    all.reserve(batches.size());
    std::int64_t first = 0;
    std::int64_t last = 0;
    for (std::size_t i = 0; i < batches.size(); ++i) {
        const std::int64_t begin = signedTicks(reference, batches[i].begin, bits);
        const std::int64_t end = std::max(begin, signedTicks(reference, batches[i].end, bits));
        first = i == 0 ? begin : std::min(first, begin);
        last = i == 0 ? end : std::max(last, end);
        all.emplace_back(begin, end);
        if (batches[i].family < kFamilies) {
            perFamily[batches[i].family].emplace_back(begin, end);
        }
    }
    out.spanMs = ticksToMs(static_cast<double>(last - first), periodNs);
    out.busyMs = ticksToMs(static_cast<double>(unionLength(all)), periodNs);
    for (std::uint32_t f = 0; f < kFamilies; ++f) {
        out.familyBusyMs[f] = ticksToMs(static_cast<double>(unionLength(perFamily[f])), periodNs);
    }
    return out;
}

} // namespace evr::gpu_timing
