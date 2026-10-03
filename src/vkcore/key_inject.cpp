#include "vkcore/key_inject.hpp"

#include "platform/key_injection/injected_keys.hpp"
#include "platform/key_injection/us_scan_codes.hpp"
#include "vkcore/import_patch.hpp"
#include "vkcore/keep_active.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace evr::vkcore {

namespace {

using key_injection::HeldKeys;
using key_injection::KeyEvent;

using GetRawInputDataFn = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using GetKeyStateFn = SHORT(WINAPI*)(int);
using GetKeyboardStateFn = BOOL(WINAPI*)(PBYTE);

std::atomic<GetRawInputDataFn> g_getRawInputData{nullptr};
std::atomic<GetKeyStateFn> g_getAsyncKeyState{nullptr};
std::atomic<GetKeyStateFn> g_getKeyState{nullptr};
std::atomic<GetKeyboardStateFn> g_getKeyboardState{nullptr};
std::once_flag g_once;
std::atomic<bool> g_installed{false}; // read by the camera hook thread (injectKey)
HANDLE g_keyboard = nullptr;
HANDLE g_mouse = nullptr;
HeldKeys g_held;
key_injection::MouseEventRing g_mouseRing;
std::atomic<std::uint32_t> g_answered{0};
std::atomic<std::uint32_t> g_mouseAnswered{0};

// The first time the game polls key state through each call, while an injected key is held.
std::atomic<bool> g_loggedAsync{false};
std::atomic<bool> g_loggedKeyState{false};
std::atomic<bool> g_loggedKeyboardState{false};

void logFirstPoll(std::atomic<bool>& flag, const char* call) {
    if (!flag.exchange(true)) {
        EVR_LOG("keys: the game polls %s while an injected key is held; the key reads as down", call);
    }
}

// Once the multiplayer guard is off, injected keys no longer read as held: polls report the real keyboard.
bool anyHeld(int virtualKey) {
    return mp_guard::allowsGameTouch() && virtualKey >= 0 && virtualKey <= 255 &&
           g_held.isDown(static_cast<std::uint8_t>(virtualKey));
}

// A mouse event that only lets buttons go: delivered even after the multiplayer guard went off, like a
// key-up, so a button the layer pressed is not left held.
bool isRelease(const key_injection::MouseEvent& event) {
    constexpr std::uint16_t kUpBits = RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_UP |
                                      RI_MOUSE_MIDDLE_BUTTON_UP | RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_UP;
    return event.buttonFlags != 0 && (event.buttonFlags & ~kUpBits) == 0 && event.dx == 0 && event.dy == 0;
}

// Answers GetRawInputData for an injected mouse event with a relative RAWMOUSE record.
UINT answerMouse(
    const key_injection::MouseEvent& event, UINT command, LPVOID data, PUINT size, UINT headerSize) {
    if (!mp_guard::allowsGameTouch() && !isRelease(event)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return static_cast<UINT>(-1);
    }
    if (!size || headerSize != sizeof(RAWINPUTHEADER)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return static_cast<UINT>(-1);
    }
    RAWINPUT input{};
    input.header.dwType = RIM_TYPEMOUSE;
    input.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE);
    input.header.hDevice = g_mouse;
    input.header.wParam = RIM_INPUT;
    input.data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
    input.data.mouse.usButtonFlags = event.buttonFlags;
    input.data.mouse.usButtonData = static_cast<USHORT>(event.wheel);
    input.data.mouse.lLastX = event.dx;
    input.data.mouse.lLastY = event.dy;
    const UINT needed =
        command == RID_HEADER ? static_cast<UINT>(sizeof(RAWINPUTHEADER)) : input.header.dwSize;
    if (!data) {
        *size = needed;
        return 0;
    }
    if (*size < needed) {
        *size = needed;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return static_cast<UINT>(-1);
    }
    std::memcpy(data, &input, needed);
    const std::uint32_t n = g_mouseAnswered.fetch_add(1) + 1;
    if (n <= 2) {
        EVR_LOG("keys: the game read an injected mouse event (%d, %d, buttons 0x%x, wheel %d)", event.dx,
                event.dy, event.buttonFlags, event.wheel);
    }
    return needed;
}

bool keepDesktopCursor();
std::atomic<std::uint64_t> g_realInputDropped{0};

UINT WINAPI hookedGetRawInputData(HRAWINPUT handle, UINT command, LPVOID data, PUINT size, UINT headerSize) {
    if (const auto mouse = g_mouseRing.take(reinterpret_cast<std::uintptr_t>(handle))) {
        return answerMouse(*mouse, command, data, size, headerSize);
    }
    const auto event = key_injection::decodeHandle(reinterpret_cast<std::uintptr_t>(handle));
    if (!event) {
        // The person's own mouse and keyboard reach the game only while its window is in front: the layer
        // keeps the game believing it is active, and a record that arrives anyway (the game took the mouse
        // before losing the foreground) would move the player while someone works on the desktop. The
        // record is read as usual (the game's input loop expects that) and then neutralised: no motion, no
        // wheel, no presses; releases stay, so nothing is left held.
        const UINT result = g_getRawInputData.load()(handle, command, data, size, headerSize);
        if (result != static_cast<UINT>(-1) && command == RID_INPUT && data &&
            result >= sizeof(RAWINPUTHEADER) && keepDesktopCursor()) {
            auto* input = static_cast<RAWINPUT*>(data);
            bool changed = false;
            if (input->header.dwType == RIM_TYPEMOUSE &&
                result >= sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE)) {
                constexpr USHORT kReleases = RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_UP |
                                             RI_MOUSE_MIDDLE_BUTTON_UP | RI_MOUSE_BUTTON_4_UP |
                                             RI_MOUSE_BUTTON_5_UP;
                RAWMOUSE& m = input->data.mouse;
                changed = m.lLastX != 0 || m.lLastY != 0 || (m.usButtonFlags & ~kReleases) != 0;
                m.lLastX = 0;
                m.lLastY = 0;
                m.usButtonFlags &= kReleases;
                m.usButtonData = 0;
            } else if (input->header.dwType == RIM_TYPEKEYBOARD &&
                       result >= sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD)) {
                RAWKEYBOARD& k = input->data.keyboard;
                changed = !(k.Flags & RI_KEY_BREAK);
                k.Flags |= RI_KEY_BREAK;
                k.Message = WM_KEYUP;
            }
            if (changed && g_realInputDropped.fetch_add(1) == 0) {
                EVR_LOG(
                    "keys: the desktop's mouse or keyboard reached the game while it is not in the "
                    "foreground; "
                    "its motion and presses are dropped (only the layer's own input moves the game then)");
            }
        }
        return result;
    }
    if (!key_injection::deliverable(*event, mp_guard::allowsGameTouch())) {
        // A key-down posted just before the guard went off is not delivered; releases still are.
        SetLastError(ERROR_INVALID_HANDLE);
        return static_cast<UINT>(-1);
    }
    if (!size || headerSize != sizeof(RAWINPUTHEADER)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return static_cast<UINT>(-1);
    }
    RAWINPUT input{};
    input.header.dwType = RIM_TYPEKEYBOARD;
    input.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD);
    input.header.hDevice = g_keyboard;
    input.header.wParam = RIM_INPUT;
    // The game reads the scan code with its E0 prefix (arrows, Home, End and the like are E0 keys), not
    // the virtual key, and numbers keys by their place: a key's US-keyboard scan code whatever the layout
    // (us_scan_codes.hpp). A key without one takes the layout's.
    key_injection::ScanCode code;
    if (const auto us = key_injection::usScanCode(event->virtualKey)) {
        code = *us;
    } else {
        const UINT scan = MapVirtualKeyW(event->virtualKey, MAPVK_VK_TO_VSC_EX);
        code = {static_cast<std::uint8_t>(scan & 0xFFu), (scan & 0xFF00u) == 0xE000u};
    }
    input.data.keyboard.MakeCode = code.make;
    input.data.keyboard.Flags =
        static_cast<USHORT>((event->down ? RI_KEY_MAKE : RI_KEY_BREAK) | (code.e0 ? RI_KEY_E0 : 0));
    input.data.keyboard.VKey = event->virtualKey;
    input.data.keyboard.Message = event->down ? WM_KEYDOWN : WM_KEYUP;
    const UINT needed =
        command == RID_HEADER ? static_cast<UINT>(sizeof(RAWINPUTHEADER)) : input.header.dwSize;
    if (!data) {
        *size = needed;
        return 0;
    }
    if (*size < needed) {
        *size = needed;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return static_cast<UINT>(-1);
    }
    std::memcpy(data, &input, needed);
    const std::uint32_t n = g_answered.fetch_add(1) + 1;
    if (n <= 4) {
        EVR_LOG("keys: the game read injected key 0x%02x %s", event->virtualKey, event->down ? "down" : "up");
    }
    return needed;
}

SHORT WINAPI hookedGetAsyncKeyState(int virtualKey) {
    const SHORT polled = g_getAsyncKeyState.load()(virtualKey);
    if (!anyHeld(virtualKey)) {
        return polled;
    }
    logFirstPoll(g_loggedAsync, "GetAsyncKeyState");
    return g_held.mergeKeyState(virtualKey, polled);
}

SHORT WINAPI hookedGetKeyState(int virtualKey) {
    const SHORT polled = g_getKeyState.load()(virtualKey);
    if (!anyHeld(virtualKey)) {
        return polled;
    }
    logFirstPoll(g_loggedKeyState, "GetKeyState");
    return g_held.mergeKeyState(virtualKey, polled);
}

BOOL WINAPI hookedGetKeyboardState(PBYTE keys) {
    const BOOL ok = g_getKeyboardState.load()(keys);
    if (ok && keys && mp_guard::allowsGameTouch()) {
        std::uint8_t before[256];
        std::memcpy(before, keys, sizeof(before));
        g_held.mergeKeyboardState(keys);
        if (std::memcmp(before, keys, sizeof(before)) != 0) {
            logFirstPoll(g_loggedKeyboardState, "GetKeyboardState");
        }
    }
    return ok;
}

void findKeyboard() {
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || count == 0) {
        return;
    }
    std::vector<RAWINPUTDEVICELIST> list(count);
    if (GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1)) {
        return;
    }
    for (const RAWINPUTDEVICELIST& d : list) {
        if (d.dwType == RIM_TYPEKEYBOARD && !g_keyboard) {
            g_keyboard = d.hDevice;
        }
        if (d.dwType == RIM_TYPEMOUSE && !g_mouse) {
            g_mouse = d.hDevice;
        }
    }
}

// The window raw input of a generic-desktop usage (0x06 keyboard, 0x02 mouse) is registered to, if the
// game named one.
HWND registeredWindow(USHORT usage) {
    UINT count = 0;
    GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE));
    if (count == 0) {
        return nullptr;
    }
    std::vector<RAWINPUTDEVICE> devices(count);
    if (GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE)) ==
        static_cast<UINT>(-1)) {
        return nullptr;
    }
    for (const RAWINPUTDEVICE& d : devices) {
        if (d.usUsagePage == 0x01 && d.usUsage == usage) {
            return d.hwndTarget;
        }
    }
    return nullptr;
}

HWND registeredKeyboardWindow() {
    return registeredWindow(0x06);
}

// The guard's trip listener: new key-downs are already refused (the latch is closed), so post one release
// for each key the layer holds; the game's key state goes back to the real keyboard's.
void releaseHeldKeysOnTrip() {
    const std::vector<std::uint8_t> released = g_held.releaseAll();
    HWND target = registeredKeyboardWindow();
    if (!target) {
        target = gameWindow();
    }
    for (const std::uint8_t vk : released) {
        const auto handle = key_injection::encodeHandle(KeyEvent{vk, false});
        const bool posted =
            target && PostMessageW(target, WM_INPUT, RIM_INPUT, static_cast<LPARAM>(handle)) != FALSE;
        EVR_LOG("keys: multiplayer guard tripped: released the held key 0x%02x%s", vk,
                posted ? ""
                       : " (posting the release failed; the game sees it up on the next real key event)");
    }
    EVR_LOG("keys: key injection off for the rest of this process (%zu held key(s) released)",
            released.size());
}

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

// True while the game must leave the desktop's cursor alone: the layer is keeping it active and its window
// is not the foreground window.
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
        return TRUE;
    }
    return g_clipCursor.load()(rect);
}

BOOL WINAPI hookedRegisterRawInputDevices(PCRAWINPUTDEVICE devices, UINT count, UINT size) {
    if (!devices || count == 0 || count > 16 || size != sizeof(RAWINPUTDEVICE) || !keepDesktopCursor()) {
        return g_registerRawInputDevices.load()(devices, count, size);
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
    return g_registerRawInputDevices.load()(copy, count, size);
}

} // namespace

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

bool installKeyInjection() {
    if (!mp_guard::allowsGameTouch()) {
        EVR_LOG("keys: key injection not installed: the multiplayer guard is %s",
                mp_policy::toString(mp_guard::state()));
        return false;
    }
    std::call_once(g_once, [] {
        findKeyboard();
        g_installed.store(replaceImport("GetRawInputData", g_getRawInputData, &hookedGetRawInputData));
        if (g_installed.load()) {
            const bool async = replaceImport("GetAsyncKeyState", g_getAsyncKeyState, &hookedGetAsyncKeyState);
            const bool state = replaceImport("GetKeyState", g_getKeyState, &hookedGetKeyState);
            const bool keyboard =
                replaceImport("GetKeyboardState", g_getKeyboardState, &hookedGetKeyboardState);
            EVR_LOG(
                "keys: key state polls replaced: GetAsyncKeyState %s, GetKeyState %s, GetKeyboardState %s",
                async ? "yes" : "no", state ? "yes" : "no", keyboard ? "yes" : "no");
        }
        UINT count = 0;
        GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE));
        std::vector<RAWINPUTDEVICE> devices(count);
        if (count) {
            GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE));
        }
        for (const RAWINPUTDEVICE& d : devices) {
            EVR_LOG("keys: raw input registered: usage page 0x%x usage 0x%x flags 0x%lx window %p",
                    d.usUsagePage, d.usUsage, d.dwFlags, static_cast<void*>(d.hwndTarget));
        }
        if (g_installed.load() && !mp_guard::addTripListener(&releaseHeldKeysOnTrip)) {
            EVR_LOG(
                "keys: no room in the multiplayer guard's trip listeners; held keys are not released on a "
                "trip");
        }
        EVR_LOG("keys: key injection %s (keyboard device %p)",
                g_installed.load() ? "ready (GetRawInputData import replaced)"
                                   : "unavailable (no GetRawInputData import)",
                g_keyboard);
    });
    return g_installed.load();
}

bool injectKey(std::uint8_t virtualKey, bool down, void* gameWindow) {
    if (!g_installed.load() || !mp_guard::allowsGameTouch()) {
        return false;
    }
    HWND target = registeredKeyboardWindow();
    if (!target) {
        target = static_cast<HWND>(gameWindow);
    }
    if (!target) {
        return false;
    }
    // Held state first: a poll after the WM_INPUT is read must already see the key down (and, on
    // release, up).
    g_held.set(virtualKey, down);
    const auto handle = key_injection::encodeHandle(KeyEvent{virtualKey, down});
    if (PostMessageW(target, WM_INPUT, RIM_INPUT, static_cast<LPARAM>(handle)) == FALSE) {
        g_held.set(virtualKey, false);
        return false;
    }
    return true;
}

bool injectMouse(int dx, int dy, std::uint16_t buttonFlags, int wheel, void* gameWindow) {
    key_injection::MouseEvent event;
    event.dx = dx;
    event.dy = dy;
    event.buttonFlags = buttonFlags;
    event.wheel = static_cast<std::int16_t>(wheel);
    if (!g_installed.load() || (!mp_guard::allowsGameTouch() && !isRelease(event))) {
        return false;
    }
    HWND target = registeredWindow(0x02);
    if (!target) {
        target = registeredKeyboardWindow();
    }
    if (!target) {
        target = static_cast<HWND>(gameWindow);
    }
    if (!target) {
        return false;
    }
    const auto handle = g_mouseRing.push(event);
    return PostMessageW(target, WM_INPUT, RIM_INPUT, static_cast<LPARAM>(handle)) != FALSE;
}

} // namespace evr::vkcore
