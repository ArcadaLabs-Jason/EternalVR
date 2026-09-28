#pragma once

// Where the HUD's blocks sit in the game's GUI target (docs/VR_HANDS_HUD.md, "HUD regions").
//
// The HUD SWF is laid out in the 16:9 band of the GUI target that ui_layer::wideContentRect gives: on a
// target taller than 16:9 (the near-square eye image of the render size, e.g. 2064x2100 or 1280x1400) the
// band spans the whole width and is centred vertically; a 16:9 target is its own band. The UI quad shows
// that band (ETERNALVR_UI_CROP), so the blocks are measured in the same band. Measured on the rig's GUI
// captures at 2560x2100 and 2054x2068 (the same band coordinates to 0.005 in both):
//
//   health, armor, extra lives      x 0.022..0.197  y 0.838..0.960  (bottom left)
//   ammo, equipment, flame belch    x 0.781..0.944  y 0.815..0.949  (bottom right, key hints above)
//   crosshair and ability rings     x 0.405..0.592  y 0.449..0.571  (centre)
//   boss / encounter bars, markers  top and sides: stay head-locked
//
// The wrist HUD shows the corner blocks (and optionally the centre abilities) on quads at the off hand;
// the head-locked quad then shows the rest of the band, cut into rectangles around the corner blocks.

#include "ui_layer/ui_settings.hpp"

#include <cstdint>
#include <vector>

namespace evr::ui_layer {

// A rectangle in the HUD band's own coordinates: 0..1 across its width and down its height.
struct BandRect {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

enum class WristBlock : std::uint8_t {
    Vitals,    // health, armor, extra lives
    Weapon,    // ammo, equipment, flame belch
    Abilities, // the rings around the crosshair (cooldowns)
};

// The measured rectangle of a block, with a margin for animations and the key hints.
BandRect wristBlockRect(WristBlock block);

// A band rectangle in pixels of a `width` x `height` target (its band: wideContentRect), rounded outward
// and clamped to the band; empty (0 x 0) when it falls outside.
PixelRect toPixels(const BandRect& rect, std::uint32_t width, std::uint32_t height);

// The part of the target taken away from the head-locked quad for a block shown on the wrist: its
// rectangle grown to the band's edges it is anchored to (the corners reach its side and bottom edges). The
// centre abilities are never cut: under head aim the crosshair stays on the head-locked quad, under hand aim
// the crosshair mask already leaves it out.
PixelRect cutRect(WristBlock block, std::uint32_t width, std::uint32_t height);

// `whole` minus every rectangle of `cuts`, as disjoint rectangles (row bands from top to bottom, left to
// right inside a band), neighbouring pieces of the same rows merged. Empty cuts are ignored.
std::vector<PixelRect> subtractRects(const PixelRect& whole, const std::vector<PixelRect>& cuts);

// Where a rectangle of the target lands on the head-locked quad: its centre relative to the quad's
// centre and its size, in metres, for a quad `panelWidth` wide showing the part `shown` of the target
// (the UI quad's image rectangle: the band, or the whole target with ETERNALVR_UI_CROP=0; +x right, +y up).
struct PanelPiece {
    float centreX = 0.0f;
    float centreY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};
PanelPiece panelPiece(const PixelRect& rect, const PixelRect& shown, float panelWidth);

} // namespace evr::ui_layer
