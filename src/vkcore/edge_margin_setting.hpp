#pragma once

// The edge margin for this process (docs/VR_STEREO.md, "Edge margin"; features/render_size/edge_margin.hpp):
// ETERNALVR_EDGE_MARGIN in stereo (ETERNALVR_MODE=stereo), read and logged once. Its factor grows an
// automatic render size (virtual_client_settings.cpp; a fixed ETERNALVR_RENDER_SIZE is taken as it is: the
// launcher passes the size it already grew) and widens each eye's FOV (presenter_stereo.cpp, prepareEyes) by
// as much as the fitted size holds at the centre's density (edgeMarginFovFactor).
//
// The desktop window shows the view without the margin: each eye's record carries where its view lies in its
// image (EyeRecord::viewRect) and the mirror's crop cuts that part out of the frame presented
// (presenter_mirror.cpp).

#include "features/render_size/render_size.hpp"

#include <cstdint>

namespace evr::vkcore {

// 1 without a margin (not stereo, unset, off, or a value that is not one: logged).
float edgeMarginFactor();

// The factor each eye's FOV is widened by: edgeMarginFactor(), or less once the runtime's limits have held
// the render size below the grown one (noteSizeLimits), never below 1.
float edgeMarginFovFactor();

// The render size's request and the runtime's limits, whenever the limits are read (virtual_client.cpp): the
// margin the fitted size keeps (render_size::requestGrowth) becomes the FOV's factor; logged when it is less
// than asked.
void noteSizeLimits(const render_size::Request& request, const render_size::ViewLimits& limits);

// The eye image's real size, every 10 s (presenter_window.cpp): logged once when it is smaller than the size
// the FOV's factor was fitted to (noteSizeLimits). A later cut (a driver's limit, the render size turned off)
// does not change the factor: the FOV keeps it, at less detail.
void noteEyeExtent(std::uint32_t width, std::uint32_t height);

} // namespace evr::vkcore
