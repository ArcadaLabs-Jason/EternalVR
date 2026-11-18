#include "stereo_seq/shared_sun.hpp"

#include <cctype>
#include <string>

namespace evr::stereo_seq {

namespace {

constexpr std::uint64_t kTickBits = 24;
constexpr std::uint64_t kTickMask = (std::uint64_t{1} << kTickBits) - 1;

// Workers list and stamp ticks concurrently, so a stamp a tick or two behind the highest one is ordinary; a
// restart of the game frame count (a map load) falls back by far more.
constexpr std::uint64_t kRestartTickGap = 16;

std::uint64_t sliceHash(std::uintptr_t light, std::uint32_t slice) {
    // splitmix64 over the light and the slice.
    std::uint64_t x = static_cast<std::uint64_t>(light) ^ (std::uint64_t{slice} << 56);
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

std::uint64_t stampWord(std::uint64_t hash, std::uint64_t tick) {
    return (hash & ~kTickMask) | (tick & kTickMask);
}

} // namespace

void SunTickStamps::stamp(std::uintptr_t light, std::uint32_t slice, std::uint64_t tick) {
    if (tick == 0) {
        return;
    }
    std::uint64_t highest = highest_.load(std::memory_order_relaxed);
    if (highest > tick && highest - tick > kRestartTickGap) {
        clear(); // the game frame count started again
        highest = 0;
    }
    while (tick > highest && !highest_.compare_exchange_weak(highest, tick, std::memory_order_relaxed)) {
    }
    const std::uint64_t hash = sliceHash(light, slice);
    slots_[hash % kSlots].store(stampWord(hash, tick), std::memory_order_relaxed);
}

bool SunTickStamps::stamped(std::uintptr_t light, std::uint32_t slice, std::uint64_t tick) const {
    if (tick == 0) {
        return false;
    }
    const std::uint64_t hash = sliceHash(light, slice);
    return slots_[hash % kSlots].load(std::memory_order_relaxed) == stampWord(hash, tick);
}

void SunTickStamps::clear() {
    for (auto& slot : slots_) {
        slot.store(0, std::memory_order_relaxed);
    }
    highest_.store(0, std::memory_order_relaxed);
}

SharedSunMode sharedSunMode(std::string_view value, bool* known) {
    std::string lower;
    for (const char c : value) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (known) {
        *known = true;
    }
    if (lower.empty() || lower == "0" || lower == "off" || lower == "false" || lower == "no") {
        return SharedSunMode::Off;
    }
    if (lower == "count") {
        return SharedSunMode::Count;
    }
    if (lower == "1" || lower == "on" || lower == "true" || lower == "yes" || lower == "shared") {
        return SharedSunMode::Shared;
    }
    if (lower == "ceiling") {
        return SharedSunMode::Ceiling;
    }
    if (known) {
        *known = false;
    }
    return SharedSunMode::Off;
}

const char* sharedSunModeName(SharedSunMode mode) {
    switch (mode) {
    case SharedSunMode::Off:
        return "off";
    case SharedSunMode::Count:
        return "count";
    case SharedSunMode::Shared:
        return "shared";
    case SharedSunMode::Ceiling:
        return "ceiling";
    }
    return "?";
}

} // namespace evr::stereo_seq
