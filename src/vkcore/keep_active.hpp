#pragma once

// The game's window: placement before its first swapchain, and keeping the game active while VR runs.
//
// DOOM pauses whenever its window is deactivated. While the XR session runs, the game's window
// procedure does not see deactivation, so desktop focus changes (the owner's other windows, the rig
// scripts) leave the game running.

#include <windows.h>

namespace evr::vkcore {

// The game's window as recorded by setGameWindow (xr_presenter.hpp), or nullptr.
HWND gameWindow();

// Subclasses the game's window (once) and starts swallowing deactivation; tells the game it is active
// if it already saw a deactivation.
void enableKeepActive();

// Lets deactivation reach the game again. The subclass stays (it passes everything through).
void disableKeepActive();

// ETERNALVR_WINDOW=x,y,width,height: places the game's window (client area of that size) once, before
// its first swapchain.
void placeGameWindow(HWND hwnd);

} // namespace evr::vkcore
