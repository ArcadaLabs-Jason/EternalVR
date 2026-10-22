#pragma once

// Where the game drew its weapon wheel, from the GUI image itself (docs/VR_MENUS.md, "Where the wheel
// shows").
//
// The wheel's size and place in the HUD's 16:9 band are not fixed: on the rig's GUI captures (2048x2208,
// 1760x1400, 1440x1600, 2048x864) it spans x 0.655..0.938, y 0.229..0.730 of the band, but a player's
// 1736x1896 capture had it at x 0.604..0.935, y 0.182..0.769 (17% larger, about the same right edge), with
// the corner blocks of that HUD larger too. A fixed rectangle cut such a wheel: its left edge, top and bottom
// stayed on the head-locked HUD beside the centred quad.
//
// So while the wheel is up the presenter reads back the alpha of the band's right part on a coarse grid
// (vkcore/wheel_probe.hpp) and takes the wheel's rectangle from what was drawn: the connected opaque shape
// that crosses the band's row 0.48 (the wheel's centre row) right of the middle, with the size and shape of a
// wheel. Its bounding rectangle, grown by the grid's step and a margin, is the wheel's quad and the cut in
// the head-locked HUD.
//
// Pure: no Vulkan or OpenXR.

#include "ui_layer/ui_settings.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace evr::ui_layer {

// The band's part searched for the wheel starts at this fraction of its width (the crosshair's rings end at
// 0.592) and runs to its right edge, over its whole height.
inline constexpr float kWheelSearchFrom = 0.45f;
// The grid: every kWheelGridStep-th pixel of every kWheelGridStep-th row of the search rectangle.
inline constexpr std::uint32_t kWheelGridStep = 4;
// A sample with at least this alpha (premultiplied RGBA8) is drawn.
inline constexpr std::uint8_t kWheelMinAlpha = 16;
// The wheel's centre row, as a fraction of the band's height (0.48 on every capture, either size).
inline constexpr float kWheelCentreRow = 0.48f;

// The part of a `width` x `height` GUI target searched for the wheel (in the band, wideContentRect).
PixelRect wheelSearchRect(std::uint32_t width, std::uint32_t height);

// Alpha samples of a search rectangle: the sample (column c, row r) is the pixel (area.x + c * step,
// area.y + r * step); `alpha` holds rows x columns of them, row by row.
struct AlphaGrid {
    PixelRect area;
    std::uint32_t step = kWheelGridStep;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::vector<std::uint8_t> alpha;
};

// The grid's size for `area` sampled every `step` pixels (columns, rows), for the readback's layout.
std::uint32_t gridColumns(const PixelRect& area, std::uint32_t step);
std::uint32_t gridRows(const PixelRect& area, std::uint32_t step);

// Working memory for measureWheel, kept between readings so they do not allocate.
struct WheelScratch {
    struct Box {
        std::uint32_t c0 = 0;
        std::uint32_t r0 = 0;
        std::uint32_t c1 = 0; // inclusive
        std::uint32_t r1 = 0; // inclusive
    };
    std::vector<std::uint32_t> label; // per sample: its shape's number, 0 for none
    std::vector<std::uint32_t> stack;
    std::vector<Box> boxes; // per shape number
};

// The wheel's rectangle in a `width` x `height` target from its grid. The wheel is the connected drawn shape
// (8-neighbours) holding the first drawn sample on the centre row, from the band's x 0.93 leftwards, whose
// shape is a wheel's: clear of the search rectangle's top and bottom (a full-screen layer is not) and of its
// left edge, between 10% and 60% of the band's width across and as wide as tall within a third, once a shape
// more than 6% wider than tall is cut to its right part as wide as it is tall (the crosshair's rings
// touching it on the left are not the wheel); the next such sample when one is not. To its box are added
// the shapes lying wholly within 2.5% of its height around it (a part of the wheel apart from its ring).
// Grown by the grid's step (a drawn pixel between samples) and a margin of 1% of the band's width, inside
// the band. nullopt when the grid holds no wheel (it is not drawn yet) or is not this target's
// (wheelSearchRect). `scratch` (optional) keeps the working memory between calls.
std::optional<PixelRect> measureWheel(const AlphaGrid& grid,
                                      std::uint32_t width,
                                      std::uint32_t height,
                                      WheelScratch* scratch = nullptr);

// One measurement as the readback publishes it: numbered from 1 in order, for a `width` x `height` target.
struct WheelMeasurement {
    std::uint64_t serial = 0; // 0: none yet
    PixelRect rect;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// The wheel's rectangle for each headset frame while it is centred. Within one time up it only grows: every
// measurement of the target shown is merged into it, so the quad holds all of the wheel the game drew
// meanwhile (a highlighted segment's bar) and nothing under the player's eyes is cut away again. A time up
// starts from the last one's rectangle (same target size), so the wheel is centred from its first frame.
// That rectangle is kept while this time up's measurements (merged) agree with it, every edge within
// kWheelRectTolerance of their width. When they lie partly outside it, or inside it but smaller for
// kWheelShrinkReadings readings, the HUD is laid out anew (a HUD scale changed) and they replace it; a
// wheel still opening (smaller, then agreeing) never shrinks it. A target resized while the wheel is up
// drops the rectangle: one of the old size does not fit the new target. nullopt before any measurement of
// this target size: the wheel then stays where the game draws it, whole, on the head-locked HUD.
class WheelRectTracker {
public:
    // `centred`: this frame's WheelCentring state; `latest`: the newest measurement published.
    std::optional<PixelRect>
    update(bool centred, std::uint32_t width, std::uint32_t height, const WheelMeasurement& latest);

private:
    std::uint64_t seen_ = 0; // measurements up to this one are merged or belong to an earlier time up
    std::optional<PixelRect> rect_;
    std::uint32_t rectWidth_ = 0; // the target size rect_ and fresh_ belong to
    std::uint32_t rectHeight_ = 0;
    bool centred_ = false;
    bool carried_ = false;           // rect_ is the last time up's, not yet confirmed by this one's
    std::optional<PixelRect> fresh_; // this time up's measurements, merged
    std::uint32_t smaller_ = 0;      // readings with fresh_ inside the carried rect_ but not agreeing
    // The last time up's rectangle and its target size.
    std::optional<PixelRect> last_;
    std::uint32_t lastWidth_ = 0;
    std::uint32_t lastHeight_ = 0;
};

// How far (a fraction of the measured rectangle's width) each edge of the last time up's rectangle may lie
// from this time up's measurements and still be kept: a highlighted segment's bar reaches 18 px past a
// 580 px wheel (3%) on the rig's 2048x2208 captures.
inline constexpr double kWheelRectTolerance = 0.05;
// How many readings smaller than the last time up's rectangle replace it.
inline constexpr std::uint32_t kWheelShrinkReadings = 15;

// `inner` lies wholly inside `outer`.
bool containsRect(const PixelRect& outer, const PixelRect& inner);

// How often the readback reads the wheel while it is up. Every frame from the time it comes up until
// kWheelStableReadings readings in a row found the same rectangle, then every kWheelSlowEvery-th frame; a
// reading that finds another rectangle (or none) goes back to every frame. On the rig the first reading of
// a time up already held the whole wheel, and a highlighted segment's bar (18 px on a 580 px wheel) lies
// inside the rectangle's margin (the grid's step and 1% of the band's width), so the slower reads only
// catch a layout that changes while the wheel is up.
class WheelReadCadence {
public:
    // Per GUI copy, `wheelUp` its frame's state: whether to read this one.
    bool next(bool wheelUp);
    // Per reading, in the order they were made: the rectangle it found.
    void measured(const std::optional<PixelRect>& rect);

private:
    bool up_ = false;
    std::uint32_t same_ = 0;  // readings in a row with the same rectangle
    std::uint32_t since_ = 0; // frames since the last read, once stable
    std::optional<PixelRect> last_;
};

inline constexpr std::uint32_t kWheelStableReadings = 3;
inline constexpr std::uint32_t kWheelSlowEvery = 8;

// The smallest rectangle holding both (an empty one is ignored).
PixelRect unionRect(const PixelRect& a, const PixelRect& b);

// Every edge of `kept` within kWheelRectTolerance of `measured`'s width of the same edge of `measured`.
bool closeRects(const PixelRect& kept, const PixelRect& measured);

} // namespace evr::ui_layer
