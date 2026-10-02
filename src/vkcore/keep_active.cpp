// Keeps the game active while VR runs (keep_active.hpp).

#include "vkcore/keep_active.hpp"

#include "vkcore/key_inject.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mirror_place.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/virtual_client.hpp"
#include "vkcore/window_cap.hpp"
#include "vkcore/xr_presenter.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <mutex>
#include <string>

namespace evr::vkcore {

namespace {

std::atomic<HWND> g_gameWindow{nullptr};
std::atomic<bool> g_keepActive{false};
std::atomic<std::uint32_t> g_swallowed{0};

// Windows this layer subclassed, each with the procedure it replaced. A record is complete (window and
// procedure stored) before the subclass is installed, so the replacement always finds its original;
// records are never removed (the subclass stays for the life of the window, passing everything through
// while keep-active is off). The game normally has one window; a new one (recreated window) gets its own.
struct Subclass {
    std::atomic<HWND> window{nullptr};
    std::atomic<WNDPROC> original{nullptr};
    std::atomic<bool> unicode{true};
};
std::array<Subclass, 4> g_subclasses;
std::mutex g_subclassMutex; // installs only

LRESULT CALLBACK keepActiveWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

const Subclass* findSubclass(HWND hwnd) {
    for (const Subclass& s : g_subclasses) {
        if (s.window.load(std::memory_order_acquire) == hwnd) {
            return &s;
        }
    }
    return nullptr;
}

// Subclasses `hwnd` unless it already is. False (logged) when that fails.
bool subclass(HWND hwnd) {
    std::lock_guard lock(g_subclassMutex);
    if (findSubclass(hwnd)) {
        return true;
    }
    Subclass* free = nullptr;
    for (Subclass& s : g_subclasses) {
        if (!s.window.load()) {
            free = &s;
            break;
        }
    }
    if (!free) {
        EVR_LOG("window: no room to subclass another game window");
        return false;
    }
    // The procedure lives in this DLL; keep the DLL loaded for the rest of the process.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&keepActiveWndProc), &self);
    const bool unicode = IsWindowUnicode(hwnd) != FALSE;
    const auto current = reinterpret_cast<WNDPROC>(unicode ? GetWindowLongPtrW(hwnd, GWLP_WNDPROC)
                                                           : GetWindowLongPtrA(hwnd, GWLP_WNDPROC));
    if (!current) {
        EVR_LOG("window: the game's window procedure cannot be read (error %lu)", GetLastError());
        return false;
    }
    free->original.store(current, std::memory_order_release);
    free->unicode.store(unicode, std::memory_order_release);
    free->window.store(hwnd, std::memory_order_release);
    const LONG_PTR previous =
        unicode ? SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&keepActiveWndProc))
                : SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&keepActiveWndProc));
    if (!previous) {
        EVR_LOG("window: subclassing the game's window failed (error %lu)", GetLastError());
        free->window.store(nullptr);
        return false;
    }
    // Another subclass may have slipped in between the read and the switch; pass on to what was there.
    free->original.store(reinterpret_cast<WNDPROC>(previous), std::memory_order_release);
    return true;
}

LRESULT CALLBACK keepActiveWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const Subclass* record = findSubclass(hwnd);
    const WNDPROC original = record ? record->original.load(std::memory_order_acquire) : nullptr;
    const bool unicode =
        record ? record->unicode.load(std::memory_order_acquire) : IsWindowUnicode(hwnd) != FALSE;
    if (!original) {
        return unicode ? DefWindowProcW(hwnd, message, wParam, lParam)
                       : DefWindowProcA(hwnd, message, wParam, lParam);
    }
    if (g_keepActive.load(std::memory_order_relaxed) &&
        hwnd == g_gameWindow.load(std::memory_order_relaxed) && mp_guard::allowsGameTouch()) {
        const bool deactivate = (message == WM_ACTIVATEAPP && wParam == FALSE) ||
                                (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) ||
                                message == WM_KILLFOCUS;
        if (deactivate) {
            const std::uint32_t n = g_swallowed.fetch_add(1) + 1;
            if (n <= 5 || n % 100 == 0) {
                EVR_LOG("window: kept the game active (message 0x%x, %u so far)", message, n);
            }
            return 0;
        }
    }
    return unicode ? CallWindowProcW(original, hwnd, message, wParam, lParam)
                   : CallWindowProcA(original, hwnd, message, wParam, lParam);
}

} // namespace

HWND gameWindow() {
    return g_gameWindow.load();
}

void enableKeepActive() {
    if (!mp_guard::allowsGameTouch()) {
        EVR_LOG("window: the multiplayer guard is %s; the game's focus handling is left alone",
                mp_policy::toString(mp_guard::state()));
        return;
    }
    HWND hwnd = g_gameWindow.load();
    if (!hwnd || !IsWindow(hwnd)) {
        EVR_LOG("window: the game's window is unknown; it will pause when it loses focus");
        return;
    }
    if (!subclass(hwnd)) {
        return;
    }
    // A game that believes it is active takes the mouse; keep the desktop's cursor while it is not in front.
    installCursorGuard();
    g_keepActive.store(true);
    EVR_LOG("window: the game stays active while VR runs (focus changes are not passed to it)");
    // If the window lost focus before the session started, the game already saw the deactivation: it
    // then ignores keyboard and mouse (the layer's injected keys too). Tell it it is active again; the
    // messages go through the game's own message loop.
    if (GetForegroundWindow() != hwnd) {
        const bool posted = PostMessageW(hwnd, WM_ACTIVATEAPP, TRUE, 0) != FALSE &&
                            PostMessageW(hwnd, WM_ACTIVATE, WA_ACTIVE, 0) != FALSE &&
                            PostMessageW(hwnd, WM_SETFOCUS, 0, 0) != FALSE;
        EVR_LOG("window: the game's window is not in the foreground; %s",
                posted ? "told the game it is active" : "posting the activation failed");
    }
}

void disableKeepActive() {
    if (g_keepActive.exchange(false)) {
        EVR_LOG("window: focus changes reach the game again");
    }
}

void setGameWindow(void* hwnd) {
    g_gameWindow.store(static_cast<HWND>(hwnd));
}

// ETERNALVR_WINDOW=x,y,width,height: places the game's window (client area of that size) before its
// first swapchain, so the game renders at a size its own window-size clamp (the primary display's work
// area) would not allow, for example on a tall virtual display. With the render size the window is only the
// mirror and goes where the mirror's options put it (virtual_client::mirrorWindow); on a device without
// present scaling the game renders at the window's size, so the window is then as large as its display's work
// area allows (window_cap::cappedWindow).
void placeGameWindow(HWND hwnd) {
    static std::once_flag once;
    std::call_once(once, [hwnd] {
        const auto capped = window_cap::cappedWindow(hwnd);
        const auto mirror = capped ? capped : virtual_client::mirrorWindow();
        std::wstring value;
        if (!mirror && (!readEnv(L"ETERNALVR_WINDOW", value) || value.empty())) {
            return;
        }
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("window: ETERNALVR_WINDOW ignored: the multiplayer guard is %s",
                    mp_policy::toString(mp_guard::state()));
            return;
        }
        int x = 0, y = 0, w = 0, h = 0;
        if (mirror) {
            x = mirror->x;
            y = mirror->y;
            w = mirror->width;
            h = mirror->height;
        } else if (swscanf_s(value.c_str(), L"%d,%d,%d,%d", &x, &y, &w, &h) != 4 || w <= 0 || h <= 0) {
            EVR_LOG("window: ETERNALVR_WINDOW '%ls' is not x,y,width,height; ignored", value.c_str());
            return;
        }
        RECT before{};
        GetClientRect(hwnd, &before);
        // Filling a display: no frame, so the client area is the whole display (the style change needs
        // SWP_FRAMECHANGED to take effect).
        const bool frameless = mirror && virtual_client::mirrorFills() && mirror_place::removeFrame(hwnd);
        RECT frame{0, 0, w, h};
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        AdjustWindowRectEx(&frame, style, FALSE, exStyle);
        int outerX = x + frame.left;
        int outerY = y + frame.top;
        mirror_place::keepFrameOnScreen(outerX, outerY, frame.right - frame.left, frame.bottom - frame.top,
                                        frame);
        const BOOL ok =
            SetWindowPos(hwnd, nullptr, outerX, outerY, frame.right - frame.left, frame.bottom - frame.top,
                         SWP_NOZORDER | SWP_NOACTIVATE | (frameless ? SWP_FRAMECHANGED : 0u));
        RECT after{};
        GetClientRect(hwnd, &after);
        EVR_LOG("window: placed at %d,%d with client %ldx%ld (was %ldx%ld)%s", x, y, after.right,
                after.bottom, before.right, before.bottom, ok ? "" : " - SetWindowPos failed");
        if (capped && ok) {
            window_cap::onPlaced(
                {static_cast<std::uint32_t>(after.right), static_cast<std::uint32_t>(after.bottom)});
        }
    });
}

} // namespace evr::vkcore
