#pragma once

// The game's own GetClientRect calls that size its swapchain and its output (docs/rig-findings/render-size.md
// section 2), answered with the render size (virtual_client.hpp). Every other GetClientRect call of the game
// (its cursor centring, the video player) gets the real client area.

#include "features/render_size/render_size.hpp"

#include <windows.h>

#include <array>
#include <cstdint>
#include <optional>

namespace evr::vkcore::client_rect {

// Locates the four calls and replaces the game's GetClientRect import (once; the caller has asked the
// multiplayer guard). False, logged, when a call is missing or the import cannot be replaced: nothing is
// changed then.
bool install();

// The window whose client area is answered, and the size it is answered with (nullopt: the real size).
void setWindow(HWND window);
void setAnswer(std::optional<render_size::Extent> size);
std::optional<render_size::Extent> answer();

// The answer the last of those calls on this thread got: the render size, or nullopt for the real client area
// (the game asks the surface's capabilities right after, on the same thread).
std::optional<render_size::Extent> lastAnswerOnThisThread();

// Calls answered with the render size so far.
std::uint64_t answers();

// Calls so far from each of the four sites (swapchain check, recreate, recreate on the window thread, window
// size), then the game's SetWindowPos calls on its window.
std::array<std::uint64_t, 5> calls();

} // namespace evr::vkcore::client_rect
