#include "stereo_seq/keep_prev.hpp"

#include <cctype>
#include <string>

namespace evr::stereo_seq {

KeepPrevMode keepPrevMode(std::string_view value) {
    std::string lower;
    for (const char c : value) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (lower == "0" || lower == "off" || lower == "false" || lower == "no") {
        return KeepPrevMode::Off;
    }
    if (lower == "count") {
        return KeepPrevMode::Count;
    }
    return KeepPrevMode::On;
}

std::uint32_t PairClock::rightPair(std::uint64_t tick) {
    std::uint64_t last = lastTick_.load(std::memory_order_acquire);
    if (last != tick && lastTick_.compare_exchange_strong(last, tick, std::memory_order_acq_rel)) {
        // Eye L of this tick stamped with `next_`; later eye L commits belong to the next pair.
        right_.store(next_.fetch_add(1, std::memory_order_acq_rel), std::memory_order_release);
    }
    return right_.load(std::memory_order_acquire);
}

bool keepPrevious(KeepPrevMode mode, Eye eye, std::uint32_t leftStamp, std::uint32_t pair) {
    return mode == KeepPrevMode::On && eye == Eye::Right && pair != 0 && leftStamp == pair;
}

} // namespace evr::stereo_seq
