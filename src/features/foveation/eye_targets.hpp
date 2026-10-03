#pragma once

// Which render targets get the eye's foveation pattern (vkcore/vrs_nv.hpp). The pattern is drawn in the
// eye's image space, so it fits only targets that cover the eye's view: the eye image itself, the game's
// half, quarter and eighth size buffers of it, and the same at the smaller render size DLSS draws the scene
// at (two thirds, 58%, half or a third of the eye image on each side, docs/rig-findings/render-size.md 2.1;
// DLAA draws it at the eye image's own size).
// The game also renders shadow maps (an 8192x8192 atlas on the rig), look-up tables and other fixed-size
// targets; foveating those would coarsen shadow edges and effects by where they happen to sit in a texture,
// not by where the player looks.

#include <cstdint>

namespace evr::foveation {

struct TargetSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Whether `target` is the eye image's size `eye` scaled by one factor from 1 down to 1/8 on both sides, each
// side within one pixel (rounded either way, also twice: a half of the DLSS size). A power-of-two square (a
// shadow map, a look-up table) counts only as the eye image or exactly 1/2, 1/4 or 1/8 of it. False while
// `eye` is not known (zero).
bool isEyeSpaceTarget(TargetSize target, TargetSize eye);

} // namespace evr::foveation
