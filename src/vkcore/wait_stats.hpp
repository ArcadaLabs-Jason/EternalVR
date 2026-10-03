#pragma once

// Waits measured over a log period: their count, total and longest, in milliseconds.

#include <algorithm>
#include <cstdint>

namespace evr::vkcore {

struct WaitStats {
    double sumMs = 0.0;
    double maxMs = 0.0;
    std::uint64_t count = 0;

    void add(double ms) {
        sumMs += ms;
        maxMs = std::max(maxMs, ms);
        ++count;
    }
    double averageMs() const { return count ? sumMs / static_cast<double>(count) : 0.0; }
};

} // namespace evr::vkcore
