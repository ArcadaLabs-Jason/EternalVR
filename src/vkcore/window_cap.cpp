// The render size capped by the window (window_cap.hpp).

#include "vkcore/window_cap.hpp"

#include "features/render_size/render_cap.hpp"
#include "vkcore/log.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/virtual_client.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

namespace evr::vkcore::window_cap {

namespace {

using render_size::Extent;
using render_size::WindowRect;

std::atomic<bool> g_noScaling{false};
std::atomic<std::uint64_t> g_placed{0}; // width << 32 | height; 0: not placed by cappedWindow

std::string sizeText(Extent e) {
    return std::to_string(e.width) + "x" + std::to_string(e.height);
}

} // namespace

bool testNoPresentScaling() {
    static const bool on = [] {
        std::wstring value;
        if (!readEnv(L"ETERNALVR_TEST_NO_PRESENT_SCALING", value) || value.empty() || value == L"0") {
            return false;
        }
        if (value != L"1") {
            EVR_LOG("test: ETERNALVR_TEST_NO_PRESENT_SCALING '%ls' is not 1; off", value.c_str());
            return false;
        }
        EVR_LOG(
            "test: the game's device is treated as having no swapchain maintenance extension, so the render "
            "size has no present scaling (ETERNALVR_TEST_NO_PRESENT_SCALING, a test knob)");
        return true;
    }();
    return on;
}

bool testHideKhrMaintenance() {
    static const bool on = [] {
        std::wstring value;
        if (!readEnv(L"ETERNALVR_TEST_HIDE_KHR_MAINTENANCE", value) || value.empty() || value == L"0") {
            return false;
        }
        if (value != L"1") {
            EVR_LOG("test: ETERNALVR_TEST_HIDE_KHR_MAINTENANCE '%ls' is not 1; off", value.c_str());
            return false;
        }
        EVR_LOG(
            "test: the game's device is treated as listing VK_EXT_swapchain_maintenance1 but not the KHR one "
            "(ETERNALVR_TEST_HIDE_KHR_MAINTENANCE, a test knob)");
        return true;
    }();
    return on;
}

void setDeviceScaling(bool available) {
    g_noScaling.store(!available, std::memory_order_release);
}

std::optional<WindowRect> cappedWindow(HWND window) {
    if (!g_noScaling.load(std::memory_order_acquire) || virtual_client::mirrorFills()) {
        return std::nullopt;
    }
    const std::optional<Extent> eye = virtual_client::plannedSize();
    if (!eye) {
        EVR_LOG("size: no present scaling on the game's device and the planned eye size is not known yet; "
                "the window goes where the mirror goes");
        return std::nullopt;
    }
    // The mirror's display, else the window's.
    HMONITOR monitor = nullptr;
    if (const auto mirror = virtual_client::mirrorWindow()) {
        const RECT r{mirror->x, mirror->y, mirror->x + mirror->width, mirror->y + mirror->height};
        monitor = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
    } else {
        monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    }
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) {
        EVR_LOG("size: no present scaling: the display's work area cannot be read; the window goes where the "
                "mirror goes");
        return std::nullopt;
    }
    const WindowRect work{info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left,
                          info.rcWork.bottom - info.rcWork.top};
    RECT frame{0, 0, 0, 0};
    AdjustWindowRectEx(&frame, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
                       static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)));
    const render_size::FrameInsets insets{-frame.left, -frame.top, frame.right, frame.bottom};
    const std::optional<WindowRect> rect = render_size::largestEyeWindow(*eye, work, insets);
    if (!rect) {
        EVR_LOG("size: no present scaling: the work area %d,%d %dx%d has no room for the window; the window "
                "goes where the mirror goes",
                work.x, work.y, work.width, work.height);
        return std::nullopt;
    }
    EVR_LOG(
        "size: no present scaling on the game's device, so each eye renders at the window's size: the "
        "window takes the largest eye-shaped client area the work area %d,%d %dx%d allows, %dx%d at %d,%d "
        "(planned eye %ux%u)",
        work.x, work.y, work.width, work.height, rect->width, rect->height, rect->x, rect->y, eye->width,
        eye->height);
    return rect;
}

void onPlaced(Extent client) {
    if (client.width == 0 || client.height == 0) {
        return;
    }
    g_placed.store((static_cast<std::uint64_t>(client.width) << 32) | client.height,
                   std::memory_order_release);
    EVR_LOG("size: the game's window size cvars are held at the window's %ux%u (no present scaling)",
            client.width, client.height);
}

std::optional<Extent> placedSize() {
    const std::uint64_t placed = g_placed.load(std::memory_order_acquire);
    if (placed == 0) {
        return std::nullopt;
    }
    return Extent{static_cast<std::uint32_t>(placed >> 32), static_cast<std::uint32_t>(placed & 0xffffffffu)};
}

std::string capText(HWND window, const std::optional<Extent>& planned, const char* driverLimit) {
    RECT client{};
    if (!window || !GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0) {
        return "";
    }
    const Extent real{static_cast<std::uint32_t>(client.right), static_cast<std::uint32_t>(client.bottom)};
    std::string text = "; each eye renders at the window's ";
    if (!planned) {
        text += sizeText(real) + " (the planned size is not known yet)";
    } else if (!render_size::capped(real, *planned)) {
        text += sizeText(real) + ", not below the planned " + sizeText(*planned);
    } else {
        text += render_size::describeCap(real, *planned);
    }
    if (driverLimit) {
        text += std::string(" (a driver limit: ") + driverLimit + ")";
    }
    return text;
}

std::string rangeLimit(Extent min, Extent max) {
    return "the driver scales presented images only within " + sizeText(min) + " to " + sizeText(max) +
           "; the window is not re-placed for this";
}

void logPresentMode(VkPresentModeKHR mode,
                    const VkSurfacePresentScalingCapabilitiesKHR& scaling,
                    const VkSurfaceCapabilitiesKHR& caps) {
    EVR_LOG(
        "size: present mode %d: scaling 0x%x, gravity x 0x%x y 0x%x, scaled image %ux%u to %ux%u; surface "
        "current %ux%u, min %ux%u, max %ux%u",
        static_cast<int>(mode), scaling.supportedPresentScaling, scaling.supportedPresentGravityX,
        scaling.supportedPresentGravityY, scaling.minScaledImageExtent.width,
        scaling.minScaledImageExtent.height, scaling.maxScaledImageExtent.width,
        scaling.maxScaledImageExtent.height, caps.currentExtent.width, caps.currentExtent.height,
        caps.minImageExtent.width, caps.minImageExtent.height, caps.maxImageExtent.width,
        caps.maxImageExtent.height);
}

void reportEyeSize(Extent eye) {
    if (!virtual_client::wanted()) {
        return;
    }
    const std::optional<Extent> planned = virtual_client::plannedSize();
    if (!planned) {
        return;
    }
    const bool cap = render_size::capped(eye, *planned);
    status::field("render_planned", sizeText(*planned));
    status::field("render_capped", cap ? "1" : "0");
    if (cap) {
        EVR_LOG("size: each eye renders at %s: the render size is off, so the game renders at its window's "
                "size (render_capped=1 in eternalvr-status.txt)",
                render_size::describeCap(eye, *planned).c_str());
    }
}

} // namespace evr::vkcore::window_cap
