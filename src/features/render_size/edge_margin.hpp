#pragma once

// The edge margin's setting (docs/VR_STEREO.md, "Edge margin"; the launcher's Edge margin): how much wider
// than its view each eye renders in stereo, so a runtime that reprojects a frame to a newer head pose (a fast
// head turn) has picture past the view's edges instead of black. It is a factor on the eye FOV's tangents
// (xr_math/edge_margin.hpp); an automatic render size grows by the same factor on both sides, so the centre
// keeps its detail and the pixel count grows by its square.
//
// Plain logic: no Windows, Vulkan or OpenXR here.

#include <optional>
#include <string>
#include <string_view>

namespace evr::render_size {

// The launcher's sizes. On a Quest 3 (54/40/44/55 degrees) they add about 2.5, 5 and 7 degrees past every
// edge of each eye's view, for 21, 44 and 69 % more pixels.
inline constexpr float kEdgeMarginSmall = 1.10f;
inline constexpr float kEdgeMarginMedium = 1.20f;
inline constexpr float kEdgeMarginLarge = 1.30f;
// A rig's own size: a whole percentage up to this.
inline constexpr int kMaxEdgeMarginPercent = 50;

// ETERNALVR_EDGE_MARGIN: unset, empty, `off`, `none` or `0`: 1 (none); `small`, `medium`, `large`; or a whole
// percentage 1..kMaxEdgeMarginPercent (`15`: 1.15). nullopt for anything else.
std::optional<float> parseEdgeMargin(std::wstring_view text);

// For the log: "off" or "x1.20 (medium)", "x1.15".
std::string describeEdgeMargin(float factor);

} // namespace evr::render_size
