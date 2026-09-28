#pragma once

// Placement of a flat "cinema" screen: the game's 2D frame shown on one quad layer in front of the
// player. Used by the first-light presenter before the stereo path exists.

#include "common/pose.hpp"

#include <cstdint>
#include <optional>

namespace evr::xr_math {

struct QuadSize {
    float width = 0.0f;
    float height = 0.0f;
};

// Size in metres of a screen `widthMetres` wide showing an image of the given pixel size, keeping the
// image's aspect ratio. Empty for a zero-sized image or a non-positive width.
std::optional<QuadSize>
cinemaQuadSize(std::uint32_t pixelWidth, std::uint32_t pixelHeight, float widthMetres);

// Pose of a screen `distance` metres in front of `head`, at head height. Only the head's yaw is used,
// so looking up or down at start-up does not tilt the screen; the screen faces back toward the head
// (a quad layer's visible side faces its local +Z).
Pose cinemaQuadPose(const Pose& head, float distance);

// The FOV (degrees) a cutscene is drawn with for the flat screen (ETERNALVR_CINEMA_ASPECT): the view a flat
// display of `aspect` (width / height) shows, fitted to the width of the game's `width` x `height` image and
// extended above and below it with square pixels, so the image's centred band of that aspect is exactly
// that view. The game keeps its vertical FOV across aspects (95 x 63.09 at 3840x2160, 63.09 x 63.09 at
// 2048x2100: below 1:1 it stops narrowing the horizontal), so the flat view's horizontal FOV comes from the
// vertical one; a game FOV already taller than the horizontal one (the game kept the width) keeps its
// horizontal FOV. nullopt (keep the game's) for an aspect of 0 or less, an image no taller than that
// aspect, or FOVs out of range.
struct CinemaFov {
    float fovX = 0.0f;
    float fovY = 0.0f;
};
std::optional<CinemaFov>
cinemaFov(float gameFovX, float gameFovY, std::uint32_t width, std::uint32_t height, double aspect);

} // namespace evr::xr_math
