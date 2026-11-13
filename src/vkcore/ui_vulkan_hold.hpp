#pragma once

// Internal to the UI layer's Vulkan hooks (ui_vulkan.cpp): the depth layer's queue hold and source-image
// watch (ui_vulkan.hpp's holdQueues and watchDestroy, ui_vulkan_hold.cpp).

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>

namespace evr::vkcore::ui_vulkan {

struct HoldWait {
    VkSemaphore timeline = VK_NULL_HANDLE;
    std::uint64_t value = 0;
};
// The wait the game's submit on `queue` is to take first (once per depth read), or nullopt.
std::optional<HoldWait> holdFor(VkQueue queue);
// That wait batch was put ahead of the game's submit on `queue` (only then is the queue marked as having
// waited).
void noteHoldAdded(VkQueue queue, const HoldWait& wait);
// The game destroys `image`.
void noteDestroyed(VkImage image);

} // namespace evr::vkcore::ui_vulkan
