#pragma once

// The edge margin (docs/VR_STEREO.md, "Edge margin"): each eye renders a little more than its view, and the
// projection layer is submitted with that wider FOV, so a runtime that reprojects the image to a newer head
// pose has picture to show past the view's edges instead of black.
//
// The FOV is widened in tangent space: each of its four tangents times the same factor. The image plane
// z = -1 then grows by that factor on both axes, so an image grown by the same factor keeps the size of its
// pixels there and the centre keeps its detail. The view's own part of the image is the same rectangle on
// that plane, which is what viewRectInImage gives (the desktop window shows it).

#include "xr_math/fov.hpp"

#include <cstdint>
#include <optional>

namespace evr::xr_math {

// `fov` with each tangent times `factor`. nullopt when `factor` is not a finite number of at least 1, or
// when `fov` has no projection (a half-angle at or past 90 degrees, no width or height, NaN).
std::optional<Fov> widenFov(const Fov& fov, float factor);

// A rectangle in an image as fractions of its size: x from the left edge, y from the top (image rows run
// down, as Vulkan's NDC y does).
struct ImageRect {
    double x = 0.0;
    double y = 0.0;
    double width = 1.0;
    double height = 1.0;
};

// Where the part of an image rendered with `rendered` that `view` sees lies, as fractions of the image.
// nullopt when either FOV has no projection or `rendered` does not hold `view` (a tolerance of 1e-6 in
// tangent).
std::optional<ImageRect> viewRectInImage(const Fov& rendered, const Fov& view);

// The view's part of an eye image that has a margin: viewRectInImage when `rendered` is wider than `view` on
// some side (more than 1e-6 in tangent), nullopt when it is the view itself (no margin: the whole image is
// the view) or does not hold it.
std::optional<ImageRect> marginViewRect(const Fov& rendered, const Fov& view);

// A rectangle of whole pixels.
struct PixelBox {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    friend constexpr bool operator==(PixelBox a, PixelBox b) = default;
};

// `rect` in a `width` x `height` image, its edges rounded to the nearest pixel edge and kept inside the
// image, at least one pixel on each side (an empty image gives an empty box).
PixelBox toPixels(const ImageRect& rect, std::uint32_t width, std::uint32_t height);

} // namespace evr::xr_math
