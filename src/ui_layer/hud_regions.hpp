#pragma once

// Where the HUD's blocks sit in the game's GUI target (docs/VR_HANDS_HUD.md, "HUD regions").
//
// The HUD SWF is laid out in the 16:9 band of the GUI target that ui_layer::wideContentRect gives: on a
// target taller than 16:9 (the near-square eye image of the render size, e.g. 2064x2100 or 1280x1400) the
// band spans the whole width and is centred vertically; on a wider target it spans the whole height and is
// centred horizontally; a 16:9 target is its own band. The UI quad shows that band (ETERNALVR_UI_CROP), so
// the blocks are found in the same band (the same band coordinates to 0.005 at every target size the rig
// captured, 1280x720 to 2560x2100).
//
// The corner blocks' size and place follow the game's HUD Scale (swf_anchorProfile; VR holds Large,
// stereo_seq::stereoHudScaleCvar) and what they hold (the extra lives row, the equipment row and its key
// hints come and go), so they are measured where the game drew them (hud_blocks.hpp). Rig GUI captures, band
// coordinates of what was drawn:
//
//   health, armor, extra lives    Small  x 0.038..0.175  y 0.867..0.944  (no extra lives row)
//                                 Medium x 0.029..0.188  y 0.780..0.947  (with it)
//                                 Large  x 0.037..0.225  y 0.749..0.944  (with it)
//   ammo, equipment, key hints    Small  x 0.809..0.934  y 0.844..0.939
//                                 Medium x 0.793..0.936  y 0.829..0.940
//                                 Large  x 0.752..0.922  y 0.805..0.937
//   crosshair and ability rings   x 0.405..0.592  y 0.449..0.571 at every HUD Scale (centre)
//   boss / encounter bars, markers  top and sides: stay head-locked
//
// The wrist HUD shows the corner blocks (and optionally the centre abilities) on quads at the off hand, the
// weapon HUD the weapon block (and optionally the vitals) above the gun; the head-locked quad then shows the
// rest of the band, cut into rectangles around the blocks that moved.

#include "ui_layer/ui_settings.hpp"

#include <cstdint>
#include <optional>
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

// The ability rings' rectangle (they do not change with the HUD Scale), with a margin for their animations.
BandRect abilitiesRect();

// The corner blocks where the game drew them in a target (hud_blocks.hpp, with its margin), in its pixels;
// nullopt while not measured.
struct CornerBlocks {
    std::optional<PixelRect> vitals;
    std::optional<PixelRect> weapon;
};

// A band rectangle in pixels of a `width` x `height` target (its band: wideContentRect), rounded outward
// and clamped to the band; empty (0 x 0) when it falls outside.
PixelRect toPixels(const BandRect& rect, std::uint32_t width, std::uint32_t height);

// The part of the target taken away from the head-locked quad for a corner block shown on a hand, which
// that hand's quad shows: `drawn` (where the game drew the block) grown to the band's edges the block is
// anchored to (the corners reach its side and bottom edges). Empty for an empty `drawn`. The centre
// abilities are never cut (empty): under head aim the crosshair stays on the head-locked quad, and under
// hand aim there is no abilities quad (the crosshair hold leaves the rings on the head-locked quad, the mask
// takes them).
PixelRect cutRect(WristBlock block, const PixelRect& drawn, std::uint32_t width, std::uint32_t height);

// What the hand HUDs take from the target for the blocks in `moved`: each one's cutRect (empty for a block
// not in `moved`), and those cuts in the order of `moved`. `complete` only when every block in `moved` is
// measured (`drawn`); otherwise no cut at all: the hand HUD waits and the head-locked HUD stays whole.
struct HandCuts {
    PixelRect vitals;
    PixelRect weapon;
    std::vector<PixelRect> cuts;
    bool complete = false;
};
HandCuts handCuts(const std::vector<WristBlock>& moved,
                  const CornerBlocks& drawn,
                  std::uint32_t width,
                  std::uint32_t height);

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
