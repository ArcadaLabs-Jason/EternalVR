#pragma once

// The game's render size decoupled from its desktop window (T-031, docs/rig-findings/render-size.md).
//
// The game sizes its swapchain from GetClientRect of its window (and requires the surface's currentExtent to
// match), and its output size (the eye image, `_gui`) from GetClientRect on WM_WINDOWPOSCHANGED. With
// ETERNALVR_RENDER_SIZE the layer answers those four GetClientRect calls (and only those: the game's cursor
// centring keeps the real client area) with the render size, reports the same size as the surface's extent,
// and creates the swapchain with present scaling (VK_KHR_swapchain_maintenance1), so the driver scales each
// presented image into the real window, which stays small (ETERNALVR_MIRROR_WINDOW). The eye images, the
// ring and the OpenXR swapchain follow the swapchain's size as before.
//
// Fail closed: without the call sites, the import, the swapchain maintenance extensions or present scaling,
// or while the multiplayer guard does not allow touching the game, nothing is answered and the game renders
// at its window's size (ETERNALVR_WINDOW places that window as before). A guard trip turns the render size
// off again and puts the window back at ETERNALVR_WINDOW.

#include "features/render_size/render_size.hpp"
#include "vkcore/dispatch.hpp"

#include <windows.h>

#include <cstdint>
#include <optional>

namespace evr::vkcore::virtual_client {

// ETERNALVR_RENDER_SIZE is set and not `off` (read once, logged with the scale and the mirror window).
bool wanted();

// The game's window got a surface (vkCreateWin32SurfaceKHR succeeded). The first time, while the guard is
// armed, the call sites are located and the game's GetClientRect import is replaced.
void onGameSurface(const InstanceData& inst, HWND window, VkSurfaceKHR surface);
void onSurfaceDestroyed(VkSurfaceKHR surface);

// The game's device: its physical device, and whether VK_KHR_swapchain_maintenance1 is on it.
void onGameDevice(const InstanceData& inst, VkPhysicalDevice physicalDevice, bool swapchainMaintenance);

// XR worker, once the system is known: the runtime's limits (auto size, and the fixed size fitted).
void onViewLimits(const render_size::ViewLimits& limits);

// vkGetPhysicalDeviceSurfaceCapabilities(2)KHR: a game surface queried on a thread whose last client-area
// answer was the render size is reported at that size (current, minimum and maximum extent).
void adjustCapabilities(VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR& caps);

// vkCreateSwapchainKHR: a game surface's swapchain at the render size gets present scaling (`scaling`,
// chained in front of `info.pNext`). True when it did.
bool addPresentScaling(VkSwapchainCreateInfoKHR& info, VkSwapchainPresentScalingCreateInfoKHR& scaling);
// A failed create with present scaling turns the render size off (the game's next create is at its window's
// size).
void onSwapchainCreated(VkResult result, VkSwapchainKHR swapchain, bool scaled);
void onSwapchainDestroyed(VkSwapchainKHR swapchain);

// Acquire and present results for a scaled swapchain: VK_SUBOPTIMAL_KHR (the image differs from the window's
// size on purpose) is VK_SUCCESS, so the game does not recreate its swapchain every frame.
VkResult scaledResult(VkSwapchainKHR swapchain, VkResult result);
void scaledResults(const VkPresentInfoKHR& info, VkResult& result);

// Every present: the render size goes off once the multiplayer guard does not allow touching the game.
void poll();

// The render size the game's window reports now, if any.
std::optional<render_size::Extent> activeExtent();

// With the render size wanted: the mirror window's client area (ETERNALVR_MIRROR_WINDOW, moved and sized by
// ETERNALVR_MIRROR_DISPLAY, _SIZE and _CROP; mirror_place.hpp), with crop `full` cut to the eye image's shape
// once its size is known (the size answered, or a fixed render size). nullopt: the window stays where it is.
std::optional<render_size::WindowRect> mirrorWindow();

// With the render size wanted and ETERNALVR_MIRROR_SIZE=fill on a display that exists: mirrorWindow() is a
// whole display, and the window goes there without its frame.
bool mirrorFills();

// The width / height of the eye image's centred band the window shows (ETERNALVR_MIRROR_CROP) while the
// game's swapchain is stretched into the window for it; 0 for the whole image (letterboxed).
double mirrorCropAspect();

struct Counters {
    std::uint64_t answers = 0;    // client areas answered with the render size
    std::uint64_t suboptimal = 0; // VK_SUBOPTIMAL_KHR results taken as success
    std::uint64_t calls = 0;      // the game's calls at the four sites
    std::uint64_t moves = 0;      // the game's SetWindowPos calls on its window
};
Counters counters();

} // namespace evr::vkcore::virtual_client
