#pragma once

// The game's swapchain images the window gate holds back from the window (presenter_window.cpp): each goes
// back to its swapchain once the ring copy that last used it has finished on the GPU, also while the copies
// are off (a failed copy, or the headset gone with the ring still there); else they stay acquired and the
// game's swapchain runs out of images. Pure (no Vulkan calls): tests/vkcore/held_images_tests.cpp.

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace evr::vkcore {

// A game swapchain image whose present was handed back instead of reaching the window.
struct HeldImage {
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::uint32_t image = 0;
    std::uint64_t value = 0; // the ring copy that last used it
};

namespace held_images {

// Hands `release` each held image whose copy has finished (`value` up to `completed`, the ring's timeline),
// in the order they were held, and takes it off the list whatever the release did; the others stay held. The
// list keeps its capacity, so a hold after it does not allocate.
template <typename Release>
void handBack(std::vector<HeldImage>& held, std::uint64_t completed, Release&& release) {
    for (auto it = held.begin(); it != held.end();) {
        if (it->value > completed) {
            ++it;
            continue;
        }
        release(*it);
        it = held.erase(it);
    }
}

// Whether a present that is not copied still hands held images back: some are held and the ring, whose
// timeline their copies are on, is still there. Without the ring they go with their swapchain.
constexpr bool handsBackUncopied(bool anyHeld, bool ringReady) {
    return anyHeld && ringReady;
}

} // namespace held_images

} // namespace evr::vkcore
