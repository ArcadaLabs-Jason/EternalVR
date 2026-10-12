#pragma once

// The full-screen additive wash in the game's GUI target (docs/rig-findings/ui-layer.md, sections 12, 13).
//
// The GUI target is premultiplied RGBA8: a pixel's colour is at most its alpha, except where the game adds
// light (colour with little or no alpha). Two things do that: small HUD pieces (the pickup notification's
// icon, the glow of the health pips) and a red vignette that covers the whole target, at low health and
// for a moment on every hit (colour about (1, 0.315, 0.112) times up to 93 of 255, alpha 0). On the flat
// screen that vignette tints the scene; on the head-locked UI quad it lights a red sheet in front of the
// world.
//
// The vignette lies behind the HUD: where a HUD piece covers alpha of a pixel, the pixel shows the
// vignette's light times (1 - alpha) (rig captures: the light a hit adds to a covered pixel follows that
// share at every coverage). So a pixel holding colour c at coverage alpha can carry at most
// (c + 1/2) / (1 - alpha) of it (washBound; c is the light rounded to a byte, which under high coverage is
// only 0 to 2), and a wash w puts w * (1 - alpha) into it (washShare).
//
// The wash is told from the pieces by its extent: it is smooth and fills every part of the target, the
// pieces are small. The rule, per colour channel c:
//   floor(b)     = min of washBound over block b           (blocks of kWashBlock x kWashBlock pixels)
//   known(b)     = block b has an uncovered (alpha 0) pixel; a block under HUD everywhere tells nothing
//   level(b)     = max of floor over the known blocks of b and its 8 neighbours, or 0 unless those with
//                  light outnumber those without kWashLitRatio to 1
//   wash(p)      = level interpolated bilinearly between the centres of the four blocks nearest p
//   c'(p)        = c(p) - min(washShare(wash(p), alpha(p)), c(p))
// A piece smaller than a block never fills one, so the floors around it are the wash's level and the piece
// keeps its own light; the wash itself is removed down to a level or two of rounding, under the HUD's
// translucent parts as well (the panels' backgrounds, the hit feedback's frame in the corner), while solid
// HUD keeps its colour (its share is near 0). A target without a wash has unlit floors almost everywhere,
// and the few lit blocks among them (a glow that fills a block) are outnumbered, so it comes out
// unchanged; a layer that covers the whole target translucently has no known block and is left alone too.
// Alpha is never changed.
//
// The interpolation keeps block edges out of the result. Additive HUD pieces larger than a block (the hex
// pattern behind the LOW HEALTH banner, the glow around the health and ammo panels) lift some blocks'
// floors above the wash; with one level per block the wash taken there changed in steps at the block
// edges, and on the HUD quad those pieces showed as squares whenever the vignette was up (rig captures in
// e1m2 and The Ancient Gods parts 1 and 2, matching the owner's report of squares on hits in the DLC with
// 0.1.46; not yet checked in a headset).
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

// The interpolation's fixed point: a pixel's position between two block centres in steps of half a pixel,
// 0 to kWashSpan (one block); both the reference and the GPU round the weighted sum the same way.
inline constexpr std::uint32_t kWashSpan = 2 * kWashBlock;

// A block's level counts when the known blocks around it with light are at least this many times those
// without: the vignette's dark centre (one unlit block among lit ones) keeps its neighbours, a lone glow
// among unlit blocks does not.
inline constexpr int kWashLitRatio = 2;

// The most wash light a pixel of colour `colour` at coverage `alpha` can hold: (colour + 1/2) / (1 - alpha),
// as `colour` is rounded, so never less than the wash that lit it; 255 under full coverage (the HUD hides the
// wash there).
std::uint8_t washBound(std::uint8_t colour, std::uint8_t alpha);

// The light a wash of `wash` puts into a pixel at coverage `alpha`: wash * (1 - alpha), rounded.
std::uint8_t washShare(std::uint8_t wash, std::uint8_t alpha);

// Removes the wash from `rgba` (width x height pixels, 4 bytes each, premultiplied, row after row) in
// place. Nothing happens when the span is smaller than width x height x 4.
void removeAdditiveWash(std::span<std::uint8_t> rgba, std::uint32_t width, std::uint32_t height);

} // namespace evr::ui_layer
