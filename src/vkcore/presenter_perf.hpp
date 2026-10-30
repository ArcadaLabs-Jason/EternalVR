#pragma once

// The game's own Performance Metrics on a quad of their own just above the HUD panel's top-right corner
// (ui_layer/perf_metrics.hpp; docs/VR_STEREO.md "Performance overlay"). XR worker only.

#include "ui_layer/perf_metrics.hpp"

#include <openxr/openxr.h>

#include <cstdint>

namespace evr::vkcore {

// The metrics' quad for the head-locked UI quad `ui` (still posed in VIEW, showing part of a `width` x
// `height` GUI target) at the game's metrics `level` (ETERNALVR_PERF_OVERLAY), written to `out`: the corner
// of the same swapchain the metrics take, on the UI quad's plane. False, and `out` untouched, when there is
// nothing to show (level 0, or the UI quad shows the corner already) or when the frame, with `layersBefore`
// layers so far and up to `layersAfter` still to come, has no room for it (ui_layer::perfQuadFits). Logs the
// first quad and the first frame it is left out of.
bool fillPerfMetricsQuad(int level,
                         std::uint32_t width,
                         std::uint32_t height,
                         const XrCompositionLayerQuad& ui,
                         std::uint32_t layersBefore,
                         std::uint32_t layersAfter,
                         XrCompositionLayerQuad& out);

} // namespace evr::vkcore
