#pragma once

// The game swapchain's present mode under Route S (docs/VR_STEREO.md, Cvars).

#include "vkcore/dispatch.hpp"

#include <cstdint>

namespace evr::vkcore {

// Route S presents twice per tick: with FIFO the tick rate is half the refresh. The game re-applies its
// own vsync setting after the command line (r_swapInterval reads back 1), so under ETERNALVR_MODE=stereo
// the layer asks for an immediate (else mailbox) present mode instead of FIFO. ETERNALVR_STEREO_VSYNC=1
// keeps the game's mode; ETERNALVR_PRESENT_IMMEDIATE=1 asks for it in any mode (mono frame-rate
// references). Returns the mode to create the swapchain with.
VkPresentModeKHR stereoPresentMode(const DeviceData& data, const VkSwapchainCreateInfoKHR& info);

// The image count to create the game's swapchain with under Route S. Immediate presents alone do not uncap
// the tick rate on a display the compositor flips to (a real monitor): with the game's two images the
// second present of a tick waits for the refresh that releases the first (stereo_seq/desktop_window.hpp).
// ETERNALVR_STEREO_SWAP_IMAGES (default 4; 0 keeps the game's count) within the surface's limits.
std::uint32_t stereoImageCount(const DeviceData& data, const VkSwapchainCreateInfoKHR& info);

// Route S, and ETERNALVR_WINDOW_PRESENTS is not `all`: the game's window takes only the presents it shows
// (the mirrored eye, at most one per two refreshes of its display; stereo_seq::WindowPresentGate); the
// others are handed back unpresented (VK_KHR_swapchain_maintenance1), so no present waits for the
// desktop display, whatever the display, the compositor or a desktop capture do with them.
bool windowPresentsGated();

} // namespace evr::vkcore
