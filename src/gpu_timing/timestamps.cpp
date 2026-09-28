#include "gpu_timing/timestamps.hpp"

namespace evr::gpu_timing {

std::uint64_t validMask(std::uint32_t validBits) {
    if (validBits == 0) {
        return 0;
    }
    if (validBits >= 64) {
        return ~std::uint64_t{0};
    }
    return (std::uint64_t{1} << validBits) - 1;
}

std::int64_t signedTicks(std::uint64_t reference, std::uint64_t t, std::uint32_t validBits) {
    const std::uint64_t mask = validMask(validBits);
    const std::uint64_t d = (t - reference) & mask;
    if (validBits >= 64) {
        return static_cast<std::int64_t>(d); // two's complement (C++20)
    }
    if (validBits == 0) {
        return 0;
    }
    const std::uint64_t half = std::uint64_t{1} << (validBits - 1);
    if (d < half) {
        return static_cast<std::int64_t>(d);
    }
    return static_cast<std::int64_t>(d) - static_cast<std::int64_t>(mask) - 1;
}

double ticksToMs(double ticks, float periodNs) {
    return ticks * static_cast<double>(periodNs) / 1e6;
}

} // namespace evr::gpu_timing
