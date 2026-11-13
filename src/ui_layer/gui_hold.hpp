#pragma once

// How long the GUI quad (the HUD, its head-locked pieces, the hand quads, the weapon wheel and the game's
// Performance Metrics) goes on showing the newest GUI image (vkcore/presenter_ui_xr.cpp, uiFresh), and the
// tally of the headset's frames in a gap in the game's images that measures it.
//
// The GUI image goes to the headset with the game image it was captured with. While the game presents and
// the capture runs, every new image brings a new GUI image. A capture that stops (a menu, a failed check on
// the game's GUI target) leaves new images without one: the quad goes kGuiFreshSeconds after the last.
// A game that is late (a checkpoint save holds its thread about half a second) hands no new image at all:
// the headset goes on showing the last one, world-locked at the head pose it was rendered with, and the GUI
// image that came with it stays up with it, for up to kGuiStallHoldSeconds; past that the quad goes as for
// a capture that stopped.

#include <cstdint>
#include <optional>

namespace evr::ui_layer {

inline constexpr double kGuiFreshSeconds = 0.25;
inline constexpr double kGuiStallHoldSeconds = 1.0;

// Whether the GUI quad shows the newest GUI image, `ageSeconds` old (since it reached the UI swapchain).
// `withShownImage`: it came with the game image the headset shows (no newer image since, the game late).
bool guiImageShown(double ageSeconds, bool withShownImage);

// Per headset frame on the head-tracked path, the frames of a gap in the game's images (no new image) once
// the GUI image is older than kGuiFreshSeconds, counted by whether the GUI quad was shown, for a gap that
// began with the GUI quad shown. A new image ends the gap: onFrame returns it once, then the tally starts
// again.
class GuiGapTally {
public:
    struct Gap {
        double gapMs = 0.0;          // from the last new image before the gap to the one that ended it
        std::uint32_t shown = 0;     // frames past kGuiFreshSeconds with the GUI quad
        std::uint32_t left = 0;      // ... without it
        double lastShownAgeMs = 0.0; // the GUI image's age on the last frame that showed it
    };

    // `newImage`: this frame shows a new game image; `secondsSinceImage`: since the last new image before
    // this frame's (the gap so far, or the whole gap on a new image); `guiAgeSeconds`: the GUI image's age,
    // negative without one; `guiShown`: the frame has the GUI quad.
    std::optional<Gap> onFrame(bool newImage, double secondsSinceImage, double guiAgeSeconds, bool guiShown);

private:
    bool armed_ = false; // the last new image was shown with the GUI quad
    Gap gap_;
};

} // namespace evr::ui_layer
