// The desktop mirror window's options (mirror_place.hpp).

#include "vkcore/mirror_place.hpp"

#include "features/render_size/mirror_window.hpp"
#include "vkcore/log.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

namespace evr::vkcore::mirror_place {

namespace {

using render_size::WindowRect;

render_size::WindowRect toRect(const RECT& r) {
    return WindowRect{r.left, r.top, r.right - r.left, r.bottom - r.top};
}

BOOL CALLBACK addMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM out) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        reinterpret_cast<std::vector<render_size::Monitor>*>(out)->push_back(render_size::Monitor{
            toRect(info.rcMonitor), toRect(info.rcWork), (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
    }
    return TRUE;
}

std::vector<render_size::Monitor> listMonitors() {
    std::vector<render_size::Monitor> monitors;
    EnumDisplayMonitors(nullptr, nullptr, addMonitor, reinterpret_cast<LPARAM>(&monitors));
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        const render_size::Monitor& m = monitors[i];
        EVR_LOG("mirror: display %zu at %d,%d %dx%d (work area %d,%d %dx%d)%s", i + 1, m.area.x, m.area.y,
                m.area.width, m.area.height, m.work.x, m.work.y, m.work.width, m.work.height,
                m.primary ? ", primary" : "");
    }
    return monitors;
}

bool isOff(const std::wstring& value) {
    return value == L"0" || _wcsicmp(value.c_str(), L"off") == 0 || _wcsicmp(value.c_str(), L"false") == 0;
}

} // namespace

Options read(const std::optional<render_size::WindowRect>& launcher) {
    Options out;
    out.rect = launcher;
    std::wstring value;
    render_size::MirrorDisplay display;
    if (readEnv(L"ETERNALVR_MIRROR_DISPLAY", value) && !value.empty()) {
        if (const auto parsed = render_size::parseMirrorDisplay(value)) {
            display = *parsed;
        } else {
            EVR_LOG(
                "mirror: ETERNALVR_MIRROR_DISPLAY '%ls' is not launcher, primary, a number or x,y; launcher",
                value.c_str());
        }
    }
    std::optional<render_size::MirrorSize> size;
    if (readEnv(L"ETERNALVR_MIRROR_SIZE", value) && !value.empty()) {
        size = render_size::parseMirrorSize(value);
        if (!size) {
            EVR_LOG("mirror: ETERNALVR_MIRROR_SIZE '%ls' is not WxH, a scale of 0.25 to 4 or fill; the "
                    "launcher's size",
                    value.c_str());
        }
    }
    if (readEnv(L"ETERNALVR_MIRROR_CROP", value) && !value.empty()) {
        if (const auto aspect = render_size::parseAspect(value)) {
            out.crop = *aspect;
        } else {
            EVR_LOG("mirror: ETERNALVR_MIRROR_CROP '%ls' is not 16:9, 16:10, W:H or full; full",
                    value.c_str());
        }
    }
    if (!size && out.crop <= 0.0 && display.choice == render_size::DisplayChoice::Launcher) {
        return out; // the launcher's rectangle as it is
    }
    // Without the launcher's rectangle: its usual 1280x720, at the primary display's corner.
    const WindowRect base = launcher.value_or(WindowRect{0, 0, 1280, 720});
    const render_size::MirrorPlacement placed =
        render_size::placeMirror(base, display, size, out.crop, listMonitors());
    out.rect = placed.rect;
    out.fill = placed.fills;
    out.centred = placed.centred;
    if (placed.fills) {
        EVR_LOG("mirror: window fills display %d at %d,%d %dx%d (borderless, %s)", placed.display,
                placed.rect.x, placed.rect.y, placed.rect.width, placed.rect.height,
                out.crop > 0.0 ? "cropped" : "stretched");
        return out;
    }
    if (size && size->fill) {
        EVR_LOG("mirror: ETERNALVR_MIRROR_SIZE=fill: the display asked for does not exist; the launcher's "
                "rectangle is kept");
    }
    EVR_LOG("mirror: window client area %d,%d %dx%d (launcher's %d,%d %dx%d)%s%s", placed.rect.x,
            placed.rect.y, placed.rect.width, placed.rect.height, base.x, base.y, base.width, base.height,
            placed.displayFound ? "" : "; the display asked for does not exist, the launcher's is kept",
            out.crop > 0.0 ? "; it shows the eye image's centred band (ETERNALVR_MIRROR_CROP)" : "");
    return out;
}

std::optional<WindowRect> shapeToEye(const Options& options, const std::optional<render_size::Extent>& eye) {
    if (!options.rect || !eye || options.crop > 0.0 || options.fill) {
        return options.rect;
    }
    const WindowRect shaped = render_size::shapeToImage(*options.rect, *eye, options.centred);
    // Logged once per shape and eye size (the size is asked before the first swapchain and again when the
    // render size turns on or changes).
    static std::atomic<std::uint64_t> logged{0};
    const std::uint64_t key = (static_cast<std::uint64_t>(shaped.width & 0xffff) << 48) |
                              (static_cast<std::uint64_t>(shaped.height & 0xffff) << 32) |
                              (static_cast<std::uint64_t>(eye->width & 0xffff) << 16) |
                              (eye->height & 0xffff);
    if (logged.exchange(key) != key) {
        EVR_LOG("mirror: the window takes the eye's shape: %dx%d at %d,%d (%ux%u eye inside %dx%d, %s)",
                shaped.width, shaped.height, shaped.x, shaped.y, eye->width, eye->height, options.rect->width,
                options.rect->height, options.centred ? "centred" : "same top-left corner");
    }
    return shaped;
}

void keepFrameOnScreen(int& x, int& y, int width, int height, const RECT& frame) {
    if (frame.top >= 0) {
        return; // no title bar
    }
    const RECT outer{x, y, x + width, y + height};
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromRect(&outer, MONITOR_DEFAULTTONEAREST), &info)) {
        return;
    }
    const WindowRect work{info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left,
                          info.rcWork.bottom - info.rcWork.top};
    const WindowRect moved = render_size::keepFrameOnScreen(WindowRect{x, y, width, height}, work);
    if (moved.x != x || moved.y != y) {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true)) {
            EVR_LOG("mirror: the window's frame is kept on the screen: moved from %d,%d to %d,%d", x, y,
                    moved.x, moved.y);
        }
        x = moved.x;
        y = moved.y;
    }
}

bool removeFrame(HWND window) {
    if (!window || !IsWindow(window)) {
        return false;
    }
    constexpr LONG_PTR kFrame = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    constexpr LONG_PTR kEdges = WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((style & kFrame) == 0 && (exStyle & kEdges) == 0) {
        return true;
    }
    // SetWindowLongPtr returns the previous value, which may be 0; the error code tells a failure apart.
    SetLastError(0);
    const bool styleSet = SetWindowLongPtrW(window, GWL_STYLE, style & ~kFrame) != 0 || GetLastError() == 0;
    SetLastError(0);
    const bool exSet = SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle & ~kEdges) != 0 || GetLastError() == 0;
    EVR_LOG("mirror: the window's frame is taken off (style 0x%llx to 0x%llx)%s",
            static_cast<unsigned long long>(style), static_cast<unsigned long long>(style & ~kFrame),
            styleSet && exSet ? "" : " - SetWindowLongPtr failed");
    return styleSet && exSet;
}

void bringToFrontOnce(HWND window) {
    static std::atomic<bool> done{false};
    if (!window || !IsWindow(window) || done.exchange(true)) {
        return;
    }
    std::wstring value;
    if (readEnv(L"ETERNALVR_MIRROR_FRONT", value) && isOff(value)) {
        EVR_LOG("mirror: the window is not raised (ETERNALVR_MIRROR_FRONT=0)");
        return;
    }
    // Topmost and back: above every other window now, and an ordinary window afterwards. Posted to the
    // window's thread (in order with the placement), never activating it.
    constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS;
    const BOOL raised = SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, flags) &&
                        SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, flags);
    EVR_LOG("mirror: the window is brought to the front once, not activated%s",
            raised ? "" : " - SetWindowPos failed");
}

} // namespace evr::vkcore::mirror_place
