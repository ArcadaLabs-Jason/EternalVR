#pragma once

// The desktop cursor guard (cursor_guard.cpp): the parts key injection and keep-active use (key_inject.hpp
// declares installCursorGuard and the counters).

namespace evr::vkcore {

// True while the game must leave the desktop's cursor alone: the layer is keeping it active and its window
// is not the foreground window.
bool keepDesktopCursor();

// Counts one of the desktop's raw input records neutralised (desktopCursorCounters); true for the first.
bool countRealInputDropped();

// The game's window lost the foreground while the layer keeps it active (keep_active.cpp, on the window's
// thread): gives back what the game took of the mouse while it was in front, its cursor clip, its window's
// mouse capture and the raw mouse's RIDEV_CAPTUREMOUSE (platform/key_injection/cursor_hold.hpp, GitHub issue
// 30). Logged in one line per release (the first 20, then every 50th).
void releaseGameCursor();
// The window is in front again (after the game handled its activation): puts back what releaseGameCursor
// undid, or what the guard kept off the desktop meanwhile, unless the game took it again itself. Not while
// the window is minimized (a taskbar click activates it before it is restored): a later activation, focus or
// restore message puts it back then.
void restoreGameCursor();

} // namespace evr::vkcore
