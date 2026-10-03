#pragma once

// The render size capped by the window (docs/rig-findings/render-size.md, section 10). Without usable present
// scaling (no swapchain maintenance extension on the game's device, as on AMD Radeon RX 5000 and 6000, or a
// surface whose scaled image range is only the window's size) the render size turns off and the game renders
// each eye at its desktop window's client area. This logs by how much, tells the launcher (the status file's
// render_planned and render_capped keys), and, when the device is known to lack present scaling before the
// window is placed, makes that window the largest eye-shaped client area its display's work area allows,
// instead of the small mirror. The game's own clamp (the work area, render-size.md section 2.1) is kept: the
// framed window never goes past the work area.

#include "features/render_size/render_size.hpp"
#include "vkcore/dispatch.hpp"

#include <windows.h>

#include <optional>
#include <string>

namespace evr::vkcore::window_cap {

// ETERNALVR_TEST_NO_PRESENT_SCALING=1 (a test knob, never set by the launcher): the game's device is
// treated as having no swapchain maintenance extension (none is added), so the fallback above runs on a
// driver that has it. Read once, logged when set.
bool testNoPresentScaling();

// ETERNALVR_TEST_HIDE_KHR_MAINTENANCE=1 (a test knob, never set by the launcher): the game's device is
// treated as not listing VK_KHR_swapchain_maintenance1, as drivers that list only the EXT one (NVIDIA
// 581.80), so the EXT path runs on a driver that has both. Read once, logged when set.
bool testHideKhrMaintenance();

// Device create, with the render size wanted: whether the game's device has present scaling at all.
void setDeviceScaling(bool available);

// Before the game's first swapchain, with the render size wanted and a device without present scaling (not
// with ETERNALVR_MIRROR_SIZE=fill): the client area for `window`, the largest of the planned eye's shape
// inside the work area of the mirror's display (logged). nullopt otherwise: the window goes where the mirror
// goes.
std::optional<render_size::WindowRect> cappedWindow(HWND window);
// The window was placed at cappedWindow(): its real client area. From then on the game's window size cvars
// are held at it (runtime_cvars.hpp), so the game and the layer agree on the window.
void onPlaced(render_size::Extent client);
std::optional<render_size::Extent> placedSize();

// For the render size off line: "; each eye renders at the window's 958x1009, 47% of the planned 2016x2112
// (a driver limit: ...)" from `window`'s real client area; empty without a window.
std::string capText(HWND window, const std::optional<render_size::Extent>& planned, const char* driverLimit);

// The driver limit for a surface whose scaled image range (`min` to `max`) leaves out the render size.
std::string rangeLimit(render_size::Extent min, render_size::Extent max);

// One present mode's scaling and the surface's raw extents, as the driver reports them.
void logPresentMode(VkPresentModeKHR mode,
                    const VkSurfacePresentScalingCapabilitiesKHR& scaling,
                    const VkSurfaceCapabilitiesKHR& caps);

// The XR swapchain's eye size: with the render size wanted, the status file gets render_planned (the planned
// eye) and render_capped (1 when the eye is smaller on either side, logged).
void reportEyeSize(render_size::Extent eye);

} // namespace evr::vkcore::window_cap
