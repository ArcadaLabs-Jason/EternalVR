#pragma once

// The desktop mirror window's options when the layer sets the render size (docs/rig-findings/render-size.md,
// section 7): the display it goes on (ETERNALVR_MIRROR_DISPLAY), its size (ETERNALVR_MIRROR_SIZE), the
// eye image's centred band only (ETERNALVR_MIRROR_CROP) and one raise to the front at start
// (ETERNALVR_MIRROR_FRONT); `fill` makes the window cover its whole display without a frame. The rectangle
// logic is features/render_size/mirror_window.hpp; this reads the settings and the displays.

#include "features/render_size/render_size.hpp"

#include <windows.h>

#include <optional>

namespace evr::vkcore::mirror_place {

struct Options {
    std::optional<render_size::WindowRect> rect; // where the window's client area goes (nullopt: where it is)
    double crop = 0.0;                           // the band's width / height, 0 for the whole eye image
    bool fill = false; // ETERNALVR_MIRROR_SIZE=fill: `rect` is a whole display, the image stretched into it
    bool centred = false; // `rect` is centred on the display ETERNALVR_MIRROR_DISPLAY chose
};

// Reads the settings and places `launcher` (ETERNALVR_MIRROR_WINDOW) on the displays Windows lists now
// (logged, with each display's number).
Options read(const std::optional<render_size::WindowRect>& launcher);

// The window's client area for an eye image of `eye`: with the whole image (crop `full`) in a window that
// does not fill its display, `options.rect` cut to the eye's shape (render_size::shapeToImage: centred where
// a display choice centred it, else at the same top-left corner), logged once per shape; the image then fills
// the window whatever the driver's scaling does. `options.rect` as it is otherwise, or without `eye` yet.
std::optional<render_size::WindowRect> shapeToEye(const Options& options,
                                                  const std::optional<render_size::Extent>& eye);

// ETERNALVR_MIRROR_SIZE=fill: takes the frame off `window` (caption, sizing border, system menu, edges) so
// its client area can be the whole display. Called before the window is placed; the frame is not put back
// (the window is only the mirror for the rest of the process). False (logged) when the style cannot be set.
bool removeFrame(HWND window);

// A framed window's outer rectangle (`frame.top` < 0: it has a title bar) at `x`, `y`: moved so its
// top-left corner lies inside the work area of the display it is on (render_size::keepFrameOnScreen), so
// the title bar can be seen and dragged. Logged once when it moves.
void keepFrameOnScreen(int& x, int& y, int width, int height, const RECT& frame);

// Once per process, unless ETERNALVR_MIRROR_FRONT=0: puts `window` in front of the other windows without
// activating it (the game's focus is left as it is, and it is never raised again).
void bringToFrontOnce(HWND window);

} // namespace evr::vkcore::mirror_place
