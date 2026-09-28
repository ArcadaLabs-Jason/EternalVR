#pragma once

// Scripted key presses for rig runs: ETERNALVR_TEST_KEYS=<ms>:<key>[:<hold ms>],... presses each key that
// many milliseconds after the first poll (the XR worker's first frame), through the layer's own key
// injection, so a run can open menus without the desktop's focus. Keys: ESC, ENTER, SPACE, TAB, UP, DOWN,
// LEFT, RIGHT, F1 to F12, A to Z, 0 to 9. Off (and free) when the variable is unset.

#include <windows.h>

namespace evr::vkcore::test_keys {

// Worker, every XR frame: presses and releases the keys that are due.
void poll(HWND gameWindow);

} // namespace evr::vkcore::test_keys
