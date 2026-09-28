#pragma once

// The full-screen additive wash in the game's GUI target (docs/rig-findings/ui-layer.md, section 12).
//
// The GUI target is premultiplied RGBA8: a pixel's colour is at most its alpha, except where the game adds
// light (colour with little or no alpha). Two things do that: small HUD pieces (the pickup notification's
// icon, the glow of the health pips) and, at low health, a red vignette that covers the whole target
// (colour about (1, 0.315, 0.112) times 0..93 of 255, alpha 0). On the flat screen that vignette tints the
// scene; on the head-locked UI quad it lights a red sheet in front of the world.
//
// The wash is told from the pieces by its extent: it is smooth and fills every part of the target, the
// pieces are small. The rule, per colour channel c:
//   excess(p)    = max(c(p) - alpha(p), 0)                 the light this pixel adds beyond its coverage
//   floor(b)     = min of excess over block b              (blocks of kWashBlock x kWashBlock pixels)
//   wash(p)      = max of floor over p's block and its 8 neighbours
//   c'(p)        = c(p) - min(wash(p), excess(p))
// A piece smaller than a block never fills one, so the floors around it are the wash's level and the piece
// keeps its own light; the wash itself is removed down to a level or two of rounding. A target without a
// wash has a floor of 0 almost everywhere and comes out unchanged. Alpha is never changed, and c' is never
// below min(c, alpha).
//
// The layer runs the same rule on the GPU (vkcore/ui_wash.cpp, its HLSL inline); this is
// the reference the tests check.

#include <cstdint>
#include <span>

namespace evr::ui_layer {

// The block side in pixels: larger than the HUD's additive pieces at the render sizes in use (the pickup
// icon is about 30 pixels on a 2056 wide target), small enough that the vignette changes by only a few
// levels across a block.
inline constexpr std::uint32_t kWashBlock = 64;

// The number of blocks along a side of `pixels` pixels.
constexpr std::uint32_t washBlocks(std::uint32_t pixels) {
    return (pixels + kWashBlock - 1) / kWashBlock;
}

// Removes the wash from `rgba` (width x height pixels, 4 bytes each, premultiplied, row after row) in
// place. Nothing happens when the span is smaller than width x height x 4.
void removeAdditiveWash(std::span<std::uint8_t> rgba, std::uint32_t width, std::uint32_t height);

} // namespace evr::ui_layer
