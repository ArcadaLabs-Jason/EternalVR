#include "gpu_timing/query_ring.hpp"

namespace evr::gpu_timing {

QueryRing::QueryRing(std::uint32_t pairs) : busy_(pairs == 0 ? 1 : pairs, false) {}

std::optional<std::uint32_t> QueryRing::acquire() {
    if (busy_[next_]) {
        return std::nullopt;
    }
    const std::uint32_t pair = next_;
    busy_[pair] = true;
    ++inUse_;
    next_ = (next_ + 1) % pairs();
    return pair;
}

void QueryRing::release(std::uint32_t pair) {
    if (pair < pairs() && busy_[pair]) {
        busy_[pair] = false;
        --inUse_;
    }
}

} // namespace evr::gpu_timing
