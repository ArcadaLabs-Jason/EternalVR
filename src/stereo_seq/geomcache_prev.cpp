#include "stereo_seq/geomcache_prev.hpp"

#include <cctype>
#include <string>

namespace evr::stereo_seq {

namespace {

// A stamp: written in bit 63, the validity in bit 62, the model time's low 30 bits in bits 32 to 61, the
// render frame below. Zero is "never updated".
constexpr std::uint64_t kWrittenBit = std::uint64_t{1} << 63;
constexpr std::uint64_t kValidBit = std::uint64_t{1} << 62;
constexpr int kTimeShift = 32;
constexpr std::uint64_t kTimeMask = 0x3FFFFFFFu;
constexpr std::uint64_t kFrameMask = 0xFFFFFFFFu;

} // namespace

GeomCachePrevMode geomCachePrevMode(std::string_view value) {
    std::string lower;
    for (const char c : value) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (lower == "0" || lower == "off" || lower == "false" || lower == "no") {
        return GeomCachePrevMode::Off;
    }
    if (lower == "count") {
        return GeomCachePrevMode::Count;
    }
    return GeomCachePrevMode::On;
}

GeomCacheStamps::GeomCacheStamps(std::size_t entries)
    : entries_(std::make_unique<Entry[]>(entries)), size_(entries) {}

void GeomCacheStamps::markLeft(
    std::size_t index, std::uintptr_t cache, std::uint32_t renderFrame, bool valid, std::uint64_t modelTime) {
    if (index >= size_) {
        return;
    }
    const std::uint64_t stamp =
        kWrittenBit | (valid ? kValidBit : 0) | ((modelTime & kTimeMask) << kTimeShift) | renderFrame;
    entries_[index].cache.store(cache, std::memory_order_relaxed);
    entries_[index].stamp.store(stamp, std::memory_order_release);
}

GeomCacheStamps::Left GeomCacheStamps::left(std::size_t index,
                                            std::uintptr_t cache,
                                            std::uint32_t renderFrame,
                                            std::uint64_t modelTime) const {
    Left out;
    if (index >= size_) {
        return out;
    }
    const std::uint64_t stamp = entries_[index].stamp.load(std::memory_order_acquire);
    if ((stamp & kWrittenBit) == 0 || (stamp & kFrameMask) != static_cast<std::uint32_t>(renderFrame - 1u) ||
        entries_[index].cache.load(std::memory_order_relaxed) != cache) {
        return out;
    }
    out.updated = true;
    out.sameTime = ((stamp >> kTimeShift) & kTimeMask) == (modelTime & kTimeMask);
    out.valid = (stamp & kValidBit) != 0;
    return out;
}

bool keepGeomCacheSlots(GeomCachePrevMode mode, Eye eye, const GeomCacheStamps::Left& left, bool animates) {
    return mode == GeomCachePrevMode::On && eye == Eye::Right && left.updated && left.sameTime && animates;
}

bool geomCachePreviousValid(bool kept, bool engineValid, bool leftValid) {
    return kept ? leftValid : engineValid;
}

} // namespace evr::stereo_seq
