#include "stereo_seq/drain_backoff.hpp"

namespace evr::stereo_seq {

std::uint64_t drainRetryMs(std::uint32_t failures) {
    std::uint64_t wait = kDrainRetryMs;
    // Doubling stops at the cap, so a long run of failures never shifts past the width of the type.
    for (std::uint32_t i = 1; i < failures && wait < kDrainRetryMaxMs; ++i) {
        wait *= 2;
    }
    return wait < kDrainRetryMaxMs ? wait : kDrainRetryMaxMs;
}

std::uint64_t DrainBackoff::failed() {
    if (failures_ < UINT32_MAX) {
        ++failures_;
    }
    return drainRetryMs(failures_);
}

} // namespace evr::stereo_seq
