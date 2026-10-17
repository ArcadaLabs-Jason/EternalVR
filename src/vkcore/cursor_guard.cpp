// The desktop cursor while the layer keeps the game active (key_inject.hpp, cursor_guard.hpp).

#include "vkcore/cursor_guard.hpp"

#include "platform/key_injection/cursor_hold.hpp"
#include "vkcore/import_patch.hpp"
#include "vkcore/keep_active.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <vector>

namespace evr::vkcore {

namespace {

std::atomic<std::uint64_t> g_realInputDropped{0};

// ---- The desktop cursor --------------------------------------------------------------------------
//
// While VR runs the layer keeps the game believing its window is active (keep_active.hpp), and a game that
// believes it is active takes the mouse: it registers raw mouse input with RIDEV_CAPTUREMOUSE, clips the
// cursor to its window and puts it back in the window's centre after every mouse movement (including the
// menu pointer's injected motion). None of that may reach the desktop while the game's window is not really
// in the foreground: the person at the PC keeps the mouse. With the window in front the game behaves as
// it always does.

using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using RegisterRawInputDevicesFn = BOOL(WINAPI*)(PCRAWINPUTDEVICE, UINT, UINT);

std::atomic<SetCursorPosFn> g_setCursorPos{nullptr};
std::atomic<ClipCursorFn> g_clipCursor{nullptr};
std::atomic<RegisterRawInputDevicesFn> g_registerRawInputDevices{nullptr};
std::once_flag g_cursorOnce;
std::atomic<std::uint64_t> g_cursorMovesKept{0};
std::atomic<std::uint64_t> g_clipsKept{0};
std::atomic<bool> g_loggedCapture{false};
// What the game took of the mouse, given back when its window loses the foreground (cursor_hold.hpp).
key_injection::CursorBook g_hold;
std::atomic<std::uint32_t> g_releases{0};
std::atomic<std::uint32_t> g_restores{0};

key_injection::CursorPoint clientOrigin() {
    POINT origin{0, 0};
    if (HWND game = gameWindow()) {
        ClientToScreen(game, &origin);
    }
    return {origin.x, origin.y};
}

key_injection::CursorRect cursorRect(const RECT& r) {
    return {r.left, r.top, r.right, r.bottom};
}

std::optional<key_injection::CursorRect> clipInForce() {
    RECT clip{};
    if (!GetClipCursor(&clip)) {
        return std::nullopt;
    }
    return cursorRect(clip);
}

// The raw mouse's registration in force (usage page 1, usage 2), if any.
std::optional<RAWINPUTDEVICE> registeredMouse() {
    UINT count = 0;
    GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE));
    if (count == 0) {
        return std::nullopt;
    }
    std::vector<RAWINPUTDEVICE> devices(count);
    if (GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE)) ==
        static_cast<UINT>(-1)) {
        return std::nullopt;
    }
    for (const RAWINPUTDEVICE& d : devices) {
        if (d.usUsagePage == 0x01 && d.usUsage == 0x02) {
            return d;
        }
    }
    return std::nullopt;
}

// Registers the raw mouse again as it is, with `flags`.
bool registerMouse(RAWINPUTDEVICE mouse, std::uint32_t flags) {
    mouse.dwFlags = flags;
    const auto real = g_registerRawInputDevices.load();
    return (real ? real : &RegisterRawInputDevices)(&mouse, 1, sizeof(mouse)) != FALSE;
}

// The layer's calls for a release or a restore. They run with the book unlocked (cursor_hold.hpp): the
// ReleaseCapture below sends WM_CAPTURECHANGED to the game's window procedure, which may clip the cursor.
class Win32CursorCalls final : public key_injection::CursorCalls {
public:
    explicit Win32CursorCalls(std::optional<RAWINPUTDEVICE> mouse) : mouse_(mouse) {}
    bool unclip() override { return ClipCursor(nullptr) != FALSE; }
    bool releaseCapture() override { return ReleaseCapture() != FALSE; }
    bool registerMouse(std::uint32_t flags) override {
        return mouse_ && vkcore::registerMouse(*mouse_, flags);
    }
    bool clip(const key_injection::CursorRect& rect) override {
        const RECT r{rect.left, rect.top, rect.right, rect.bottom};
        return ClipCursor(&r) != FALSE;
    }
    std::optional<key_injection::CursorRect> clipInForce() override { return vkcore::clipInForce(); }

private:
    std::optional<RAWINPUTDEVICE> mouse_;
};

const char* outcome(bool wanted, bool worked) {
    return wanted ? (worked ? "yes" : "failed") : "-";
}

} // namespace

bool keepDesktopCursor() {
    if (!mp_guard::allowsGameTouch()) {
        return false; // the layer no longer changes anything about the game
    }
    HWND game = gameWindow();
    if (!game) {
        return false;
    }
    HWND front = GetForegroundWindow();
    return front != game && GetAncestor(front, GA_ROOTOWNER) != GetAncestor(game, GA_ROOTOWNER);
}

bool countRealInputDropped() {
    return g_realInputDropped.fetch_add(1) == 0;
}

namespace {

BOOL WINAPI hookedSetCursorPos(int x, int y) {
    if (keepDesktopCursor()) {
        if (g_cursorMovesKept.fetch_add(1) == 0) {
            EVR_LOG(
                "keys: the game moved the desktop cursor while not in the foreground; its moves are dropped "
                "(the desktop keeps its cursor)");
        }
        return TRUE;
    }
    return g_setCursorPos.load()(x, y);
}

BOOL WINAPI hookedClipCursor(const RECT* rect) {
    if (rect && keepDesktopCursor()) {
        if (g_clipsKept.fetch_add(1) == 0) {
            EVR_LOG(
                "keys: the game clipped the desktop cursor while not in the foreground; the clip is dropped");
        }
        g_hold.clipKept(cursorRect(*rect), clientOrigin());
        return TRUE;
    }
    const BOOL ok = g_clipCursor.load()(rect);
    if (ok) {
        g_hold.gameClipped(rect ? clipInForce() : std::nullopt, clientOrigin());
    }
    return ok;
}

// The game's raw mouse registration among `devices`, for the book (cursor_hold.hpp).
void noteMouse(PCRAWINPUTDEVICE devices, UINT count, bool captureKept) {
    for (UINT i = 0; i < count; ++i) {
        if (devices[i].usUsagePage == 0x01 && devices[i].usUsage == 0x02 &&
            !(devices[i].dwFlags & RIDEV_REMOVE)) {
            g_hold.gameRegisteredMouse(devices[i].dwFlags, captureKept);
        }
    }
}

BOOL WINAPI hookedRegisterRawInputDevices(PCRAWINPUTDEVICE devices, UINT count, UINT size) {
    if (!devices || count == 0 || count > 16 || size != sizeof(RAWINPUTDEVICE) || !keepDesktopCursor()) {
        const BOOL ok = g_registerRawInputDevices.load()(devices, count, size);
        if (ok && devices && count <= 16 && size == sizeof(RAWINPUTDEVICE)) {
            noteMouse(devices, count, false);
        }
        return ok;
    }
    RAWINPUTDEVICE copy[16];
    std::memcpy(copy, devices, sizeof(RAWINPUTDEVICE) * count);
    for (UINT i = 0; i < count; ++i) {
        if (copy[i].usUsagePage == 0x01 && copy[i].usUsage == 0x02 &&
            (copy[i].dwFlags & RIDEV_CAPTUREMOUSE)) {
            copy[i].dwFlags &= ~static_cast<DWORD>(RIDEV_CAPTUREMOUSE);
            if (!g_loggedCapture.exchange(true)) {
                EVR_LOG("keys: raw mouse registered without RIDEV_CAPTUREMOUSE (the game is not in the "
                        "foreground)");
            }
        }
    }
    const BOOL ok = g_registerRawInputDevices.load()(copy, count, size);
    if (ok) {
        noteMouse(devices, count, true);
    }
    return ok;
}

} // namespace

void releaseGameCursor() {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const std::optional<RAWINPUTDEVICE> mouse = registeredMouse();
    Win32CursorCalls calls(mouse);
    const key_injection::CursorCallsDone done =
        g_hold.release(clipInForce(), mouse ? std::optional<std::uint32_t>(mouse->dwFlags) : std::nullopt,
                       GetCapture() != nullptr, calls);
    if (!done.any()) {
        return;
    }
    const std::uint32_t n = g_releases.fetch_add(1) + 1;
    if (n <= 20 || n % 50 == 0) {
        EVR_LOG(
            "keys: the game's window lost the foreground; given back: cursor clip %s, mouse capture %s, raw "
            "mouse capture %s (release %u)",
            outcome(done.unclip, done.unclipped), outcome(done.releaseCapture, done.uncaptured),
            outcome(done.mouse, done.registered), n);
    }
}

void restoreGameCursor() {
    if (!mp_guard::allowsGameTouch() || keepDesktopCursor()) {
        return; // not really in front (an activation the layer posted itself)
    }
    HWND game = gameWindow();
    if (game && IsIconic(game)) {
        return; // still minimized: its client area is off the screen; a later message puts it back
    }
    const std::optional<RAWINPUTDEVICE> mouse = registeredMouse();
    Win32CursorCalls calls(mouse);
    const key_injection::CursorCallsDone done = g_hold.restore(clientOrigin(), calls);
    if (!done.any()) {
        return;
    }
    const std::uint32_t n = g_restores.fetch_add(1) + 1;
    if (n <= 20 || n % 50 == 0) {
        EVR_LOG(
            "keys: the game's window is in front again; put back: cursor clip %s, raw mouse capture %s (%u)",
            outcome(done.clip, done.clipped), outcome(done.mouse, done.registered), n);
    }
}

DesktopCursorCounters desktopCursorCounters() {
    return {g_cursorMovesKept.load(), g_clipsKept.load(), g_realInputDropped.load()};
}

void installCursorGuard() {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    std::call_once(g_cursorOnce, [] {
        const bool move = replaceImport("SetCursorPos", g_setCursorPos, &hookedSetCursorPos);
        const bool clip = replaceImport("ClipCursor", g_clipCursor, &hookedClipCursor);
        const bool raw = replaceImport("RegisterRawInputDevices", g_registerRawInputDevices,
                                       &hookedRegisterRawInputDevices);
        EVR_LOG("keys: desktop cursor kept from the game while it is not in the foreground: SetCursorPos %s, "
                "ClipCursor %s, RegisterRawInputDevices %s",
                move ? "yes" : "no", clip ? "yes" : "no", raw ? "yes" : "no");
    });
}

} // namespace evr::vkcore
