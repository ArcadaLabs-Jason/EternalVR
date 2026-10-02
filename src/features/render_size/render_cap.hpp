#pragma once

// The render size capped by the window (docs/rig-findings/render-size.md, section 10): on a driver without
// usable present scaling the game renders each eye at its desktop window's client area, so the eye is only
// as large as that window. This is the arithmetic for the log, the status file and the window placed then.
//
// Plain logic: no Windows, Vulkan or OpenXR here.

#include "features/render_size/render_size.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace evr::render_size {

// `real` is smaller than `planned` on either side (a side planned at 0 is never capped).
bool capped(Extent real, Extent planned);

// `real` as a whole percentage of `planned`, rounded down; 100 for a side planned at 0.
std::uint32_t percentOf(std::uint32_t real, std::uint32_t planned);
// The smaller of the two sides' percentages.
std::uint32_t cappedPercent(Extent real, Extent planned);

// For the log: "958x1009, 47% of the planned 2016x2112" (one percentage when both sides agree, else
// "50% x 48%").
std::string describeCap(Extent real, Extent planned);

// The frame a window adds around its client area (AdjustWindowRectEx), each side as a positive width.
struct FrameInsets {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
};

// The largest client area of the eye image's shape whose framed window fits inside `workArea`, centred in it,
// and never larger than `eye` itself (whole pixels, at least 1 per side). nullopt for an empty eye or a work
// area too small for the frame.
std::optional<WindowRect> largestEyeWindow(Extent eye, const WindowRect& workArea, const FrameInsets& frame);

} // namespace evr::render_size
