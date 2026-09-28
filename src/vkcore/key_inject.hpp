#pragma once

// Key presses delivered to the game from inside its process, independent of desktop focus.
//
// The game reads the keyboard through raw input (WM_INPUT and GetRawInputData). The layer replaces the
// exe's import of GetRawInputData; a key event is a WM_INPUT posted to the window raw input is
// delivered to, whose handle is one GetRawInputData answers itself with a keyboard record. The exe's
// imports of GetAsyncKeyState, GetKeyState and GetKeyboardState are replaced too, so that a held injected
// key also polls as down (platform/key_injection/injected_keys.hpp). Everything else passes through
// unchanged.

#include <cstdint>

namespace evr::vkcore {

// While the layer keeps the game active (keep_active.hpp) the game's window is often not the foreground
// window, yet the game still acts as if it had the mouse: it would clip the desktop's cursor to its window,
// warp it to the window's centre after every mouse movement and capture mouse clicks. This replaces the
// exe's SetCursorPos, ClipCursor and RegisterRawInputDevices imports so that, while the window is not in
// the foreground, those calls leave the desktop alone (once; logged). In the foreground nothing changes.
void installCursorGuard();

// How many of the game's cursor moves and clips were kept off the desktop.
struct DesktopCursorCounters {
    std::uint64_t moves = 0;
    std::uint64_t clips = 0;
    std::uint64_t input = 0; // the desktop's raw mouse and keyboard records neutralised
};
DesktopCursorCounters desktopCursorCounters();

// Replaces the import (once). False, logged, when the exe has no such import or it cannot be patched.
bool installKeyInjection();

// Posts one key event (virtual-key code, down or up) to the game. False when injection is unavailable
// or there is no window to post to.
bool injectKey(std::uint8_t virtualKey, bool down, void* gameWindow);

// Posts one relative mouse event: motion in raw mouse counts, raw input's RI_MOUSE_* button flags and a
// wheel amount (WHEEL_DELTA units). The game handles it as it handles a real mouse (its menus move their
// cursor, a button becomes a key event). False when injection is unavailable.
bool injectMouse(int dx, int dy, std::uint16_t buttonFlags, int wheel, void* gameWindow);

} // namespace evr::vkcore
