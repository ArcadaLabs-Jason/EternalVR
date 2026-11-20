#pragma once

// The registers the sun's stale check hook (vkcore/shared_sun_hooks.cpp) hands back for a slice eye R keeps
// (stereo_seq/shared_sun.hpp, sharedSunKeeps).

#include <cstdint>

namespace evr::engine {

// eax takes the counter (`cmp edx, eax` finds no excess, so `ja` does not release the entry; rax's upper
// half kept), and bl loses bits 2 and 4 (`test bl, 4` and `test bl, 6` find nothing, so the slice is not
// listed; the rest of rbx kept).
struct SunKeepRegisters {
    std::uint64_t rax = 0;
    std::uint64_t rbx = 0;
};
constexpr SunKeepRegisters sunKeepRegisters(std::uint64_t rax, std::uint64_t rbx, std::uint32_t counter) {
    return {(rax & ~std::uint64_t{0xFFFFFFFF}) | counter, rbx & ~std::uint64_t{6}};
}

} // namespace evr::engine
