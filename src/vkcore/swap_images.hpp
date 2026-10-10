#pragma once

// Parallel Eye Rendering's swapchain guard (view_swap_guard.hpp): whether the colour image the screen target
// holds is still one of the swapchain's, and whether a screen pass is left out for it. Pointers are compared
// only: an image the swapchain no longer holds may be freed, so it is never read. Pure (no game), unit tested
// (tests/vkcore/swap_images_tests.cpp).

#include <algorithm>
#include <array>
#include <cstdint>

namespace evr::vkcore::swap_images {

// The swapchain's image array holds 8 entries (idSwapChain + 0x38 .. + 0x78; its semaphores follow at
// + 0xB0).
inline constexpr std::int32_t kMaxImages = 8;

// What the swapchain holds now: its images (the engine's image objects), how many there are, and the image
// the acquire sets on the screen target when there is no swapchain or the acquire failed.
struct Swapchain {
    std::array<std::uintptr_t, kMaxImages> images{};
    std::int32_t count = 0;
    std::uintptr_t fallback = 0;
};

// `colour` (the screen target's colour image, target + 0x10) is one of the swapchain's images or its
// fallback. A null colour is none of them; a count outside 0..8 is clamped to it.
inline bool holds(const Swapchain& swapchain, std::uintptr_t colour) {
    if (colour == 0) {
        return false;
    }
    if (colour == swapchain.fallback) {
        return true;
    }
    const std::int32_t count = std::clamp(swapchain.count, std::int32_t{0}, kMaxImages);
    const auto end = swapchain.images.begin() + count;
    return std::find(swapchain.images.begin(), end, colour) != end;
}

// A screen pass is left out only when it is view 0's, does not run in the finish job (its return address is
// not the finish's: that one runs right after the acquire) and its colour image is not the swapchain's.
inline bool leaveOut(bool finish, std::int32_t viewIndex, bool held) {
    return !finish && viewIndex == 0 && !held;
}

} // namespace evr::vkcore::swap_images
