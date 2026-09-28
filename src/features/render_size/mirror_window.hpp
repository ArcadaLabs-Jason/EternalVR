#pragma once

// The desktop window as a mirror of the headset (docs/rig-findings/render-size.md, section 7): which
// display it goes on, its size, and whether it shows the eye image's centred wide band. Also the band
// itself, shared with the cinema screen (ETERNALVR_CINEMA_ASPECT).
//
// Plain logic: the settings, the band of an image and the window's rectangle on a list of displays.

#include "features/render_size/render_size.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace evr::render_size {

// ETERNALVR_CINEMA_ASPECT, ETERNALVR_MIRROR_CROP: `16:9`, `16:10` or any `W:H` from 1:1 to 4:1 gives that
// width / height; `full` (also `off`, `0`) gives 0, the whole image. nullopt for anything else.
std::optional<double> parseAspect(std::wstring_view text);

// Rows of an image.
struct Band {
    std::uint32_t y = 0;
    std::uint32_t height = 0;
    friend constexpr bool operator==(Band a, Band b) = default;
};
// The centred full-width band of `aspect` (width / height) of a `width` x `height` image, its height rounded
// to whole pixels. The whole image for an aspect of 0 or less, or when the image is not taller than the band
// (0.5 % tolerance, the rounding of window sizes).
Band centredBand(std::uint32_t width, std::uint32_t height, double aspect);

// ETERNALVR_MIRROR_DISPLAY: `launcher` (or empty: the launcher's rectangle, ETERNALVR_MIRROR_WINDOW, as it
// is), `primary`, a display number `N` (1 = the first display Windows lists; the log lists them), or a
// desktop point `x,y` (the display that holds it). Every choice but `launcher` centres the window on that
// display.
enum class DisplayChoice : std::uint8_t { Launcher, Primary, Number, Point };
struct MirrorDisplay {
    DisplayChoice choice = DisplayChoice::Launcher;
    std::int32_t number = 0; // DisplayChoice::Number, from 1
    std::int32_t x = 0;      // DisplayChoice::Point
    std::int32_t y = 0;
};
std::optional<MirrorDisplay> parseMirrorDisplay(std::wstring_view text);

// ETERNALVR_MIRROR_SIZE: `WxH` (the client area, 64 to 8192 per side), a scale of the launcher's size
// (0.25 to 4) or `fill` (the whole display, taskbar included, borderless). nullopt for anything else (empty
// included: the launcher's size).
struct MirrorSize {
    Extent size;        // set for `WxH`
    float scale = 0.0f; // set for a scale
    bool fill = false;  // set for `fill`
};
std::optional<MirrorSize> parseMirrorSize(std::wstring_view text);

// A display: its whole area and its work area (without the taskbar), in desktop pixels.
struct Monitor {
    WindowRect area;
    WindowRect work;
    bool primary = false;
};

struct MirrorPlacement {
    WindowRect rect;
    bool displayFound = true; // false: the asked display does not exist, the launcher's is used
    bool fills = false;       // the window covers the whole of display `display`
    std::int32_t display = 0; // with `fills`: the display's number, from 1
    bool centred = false;     // `rect` is centred on the chosen display (every choice but `launcher`)
};

// The mirror window's client area. From the launcher's rectangle: its size replaced or scaled (`size`), cut
// to `cropAspect` when that is above 0 (the largest rectangle of that aspect inside), then put on the chosen
// display (centred in its work area) or left where the launcher put it, and scaled down uniformly to fit
// that display (its work area, or its whole area for the launcher's place). Without a size, a crop or a
// display choice, the launcher's rectangle as it is. A `fill` size gives the chosen display's whole area (the
// launcher's display for `launcher`, the display holding the launcher's corner), whatever the crop: the
// image is stretched into it. With no such display, the launcher's rectangle as it is.
MirrorPlacement placeMirror(const WindowRect& launcher,
                            const MirrorDisplay& display,
                            const std::optional<MirrorSize>& size,
                            double cropAspect,
                            const std::vector<Monitor>& monitors);

// The mirror window showing the whole eye image (no crop, not filling a display) takes the image's shape:
// the largest rectangle of `image`'s aspect inside `rect` (whole pixels, at least 1 per side), so the image
// fills the window without black bars. Centred in `rect` when `centred` (the display choice centred it),
// else at `rect`'s top-left corner (the launcher's place). `rect` as it is for an empty image or rectangle.
WindowRect shapeToImage(const WindowRect& rect, Extent image, bool centred);

// A framed window's outer rectangle moved (never resized) so its top-left corner, where the title bar is,
// lies inside `workArea`: a client area placed at a display's corner would leave the frame off the screen,
// and the window could not be dragged.
WindowRect keepFrameOnScreen(const WindowRect& outer, const WindowRect& workArea);

} // namespace evr::render_size
