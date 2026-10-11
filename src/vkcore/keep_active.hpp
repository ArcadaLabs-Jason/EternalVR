#pragma once

// The game's window: placement before its first swapchain, and keeping the game active while VR runs.
//
// DOOM pauses whenever its window is deactivated. While the XR session runs, the game's window
// procedure does not see deactivation, so desktop focus changes (the owner's other windows, the rig
// scripts) leave the game running.

#include <windows.h>

#include <string>

namespace evr::vkcore {

// The game's window as recorded by setGameWindow (xr_presenter.hpp), or nullptr.
HWND gameWindow();

// The foreground window, for the log. The desktop's keyboard reaches the game only while its window is in
// front (key_inject.hpp): a prompt that no key closes may be one whose window lost the foreground.
struct ForegroundWindow {
    bool game = false; // the game's window, or a window it owns
    std::string who;   // "the game's window", or the window's class and its process's file name (never its
                       // title, which can carry a page's or a document's name)
};
ForegroundWindow foregroundWindow();

// Subclasses the game's window (once) and starts swallowing deactivation; tells the game it is active
// if it already saw a deactivation.
void enableKeepActive();

// Lets deactivation reach the game again. The subclass stays (it passes everything through).
void disableKeepActive();

// ETERNALVR_WINDOW=x,y,width,height: places the game's window (client area of that size) once, before
// its first swapchain.
void placeGameWindow(HWND hwnd);

} // namespace evr::vkcore
