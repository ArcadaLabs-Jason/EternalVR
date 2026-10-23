#pragma once

// Where the game drew the HUD's corner blocks, from the GUI image itself (docs/VR_HANDS_HUD.md, "Where the
// corner blocks are").
//
// The wrist and weapon HUDs move the corner blocks (health and armor bottom left, ammo and equipment bottom
// right) to quads at the hands and cut them out of the head-locked HUD. Where the blocks are depends on the
// game's HUD Scale (Large, which VR holds, is about 17% larger than Medium) and on what they hold (the extra
// lives row above health and armor, the equipment row and its key hints above the ammo come and go; a
// cinematic's skip prompt sits where the ammo is): fixed rectangles made for Medium left a Large block's
// edge on the head-locked HUD (hud_regions.hpp has the numbers).
//
// So the presenter reads back the alpha of the band's two bottom corners on a coarse grid (the weapon
// wheel's readback, vkcore/wheel_probe.hpp, with its grid and labelling, wheel_bounds.hpp) and takes each
// block from what was drawn: the largest connected drawn shape in its corner clear of the corner's inner
// edges (the block's bars and icons, which their glow joins), low in the band, with every shape lying near
// it. Readings are made only in wrist or weapon mode outside menus and full-screen backdrops, and slowly once
// both blocks are found (CornerReadCadence); the head-locked HUD and the hand quads use the union of the last
// few readings (CornerRectTracker), so a block that grows (the extra lives row appears) is cut whole from its
// next reading on, and one that shrinks gives the space back a few readings later.
//
// Pure: no Vulkan or OpenXR.

#include "ui_layer/hud_regions.hpp"
#include "ui_layer/ui_settings.hpp"
#include "ui_layer/wheel_bounds.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::ui_layer {

// A corner's search rectangle: the band's bottom part from kCornerSearchTop of its height, the outer
// kCornerSearchWidth of its width (left for the vitals, right for the weapon block). The blocks reach 0.75 of
// the band's height and 0.23 of its width from their side at Large; the crosshair's rings, the interact
// prompt and the bottom middle's indicators start at 0.41 of the width from either side.
inline constexpr float kCornerSearchTop = 0.60f;
inline constexpr float kCornerSearchWidth = 0.40f;

// The search rectangle of `block` (Vitals or Weapon; empty for Abilities) in a `width` x `height` target.
PixelRect cornerSearchRect(WristBlock block, std::uint32_t width, std::uint32_t height);

// The block's rectangle in a `width` x `height` target from its corner's grid (sampled every grid.step
// pixels of cornerSearchRect, as the wheel's): of the drawn shapes (8-neighbours, alpha at least
// kWheelMinAlpha) that touch neither the search rectangle's top nor its inner edge (a full-screen layer, the
// open wheel's bottom, a long subtitle reaching in from the middle), the largest, which must reach below
// kCornerSeedLow of the band's height and be at least kCornerMinSeed of its width across; with it every other
// such shape whose box comes within kCornerGather of the band's height of the block's box, again until none
// does. The box, at most kCornerMaxSize of the band's width and height (a layer filling the corner is not a
// block), grown by the grid's step (a drawn pixel between samples) and 1% of the band's width, inside the
// band. nullopt when the grid holds no such block (the HUD is not drawn, a menu or a fade covers it: more
// than kCornerMaxDrawn of its samples drawn) or is not this target's.
inline constexpr float kCornerSeedLow = 0.85f;
inline constexpr float kCornerMinSeed = 0.05f;
inline constexpr float kCornerGather = 0.02f;
inline constexpr float kCornerMaxSize = 0.30f;
inline constexpr float kCornerMaxDrawn = 0.5f;
std::optional<PixelRect> measureCornerBlock(const AlphaGrid& grid,
                                            WristBlock block,
                                            std::uint32_t width,
                                            std::uint32_t height,
                                            WheelScratch* scratch = nullptr);

// One reading as the readback publishes it: numbered from 1 in order, for a `width` x `height` target.
struct CornerMeasurement {
    std::uint64_t serial = 0; // 0: none yet
    std::optional<PixelRect> vitals;
    std::optional<PixelRect> weapon;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// The corner blocks for each headset frame: per block, the union of its last kCornerKeptReadings
// measurements of this target size (a reading that found no block does not count). nullopt for a block
// before its first measurement of this size: it then stays where the game draws it, on the head-locked HUD.
// A new target size drops what was measured: a rectangle of the old size does not fit it. Readings that
// arrive while `passOver` (a cutscene: the blocks are not drawn, and what a corner holds then, such as the
// skip prompt, is not a block) are marked seen without counting.
inline constexpr std::size_t kCornerKeptReadings = 8;
class CornerRectTracker {
public:
    CornerBlocks
    update(std::uint32_t width, std::uint32_t height, const CornerMeasurement& latest, bool passOver = false);

private:
    struct Kept {
        std::array<PixelRect, kCornerKeptReadings> rects{};
        std::size_t count = 0; // filled entries
        std::size_t next = 0;  // where the next one goes
        void add(const PixelRect& r);
        [[nodiscard]] std::optional<PixelRect> merged() const;
    };
    std::uint64_t seen_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    Kept vitals_;
    Kept weapon_;
};

// How often the readback reads the corners: only while `wanted` (wrist or weapon mode, no menu, no
// full-screen backdrop, the wheel down); every GUI copy until a reading of this
// target size found both blocks (at most kCornerFastReadings of them: a HUD the player hid has no blocks to
// find), then every kCornerSlowEvery-th. On the rig the blocks' rectangles stayed the same from one reading
// to the next for minutes of play, changing only when what they hold changed.
inline constexpr std::uint32_t kCornerFastReadings = 30;
inline constexpr std::uint32_t kCornerSlowEvery = 30;
class CornerReadCadence {
public:
    // Per GUI copy: whether this one is to be read. It stays due until consume(): a copy that finds no
    // reading slot free (or no host buffer) leaves the read to the next one.
    bool due(bool wanted, std::uint32_t width, std::uint32_t height);
    // The due read was recorded.
    void consume();
    // Per reading, in the order they were made: its target size and whether it found both blocks.
    void measured(std::uint32_t width, std::uint32_t height, bool both);

private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool found_ = false;                           // a reading of this size found both blocks
    std::uint32_t fastLeft_ = kCornerFastReadings; // reads every copy left before that
    std::uint32_t since_ = 0;                      // GUI copies wanted since the last read, once slow
};

} // namespace evr::ui_layer
