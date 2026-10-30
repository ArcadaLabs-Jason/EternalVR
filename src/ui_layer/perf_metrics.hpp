#pragma once

// The game's own Performance Metrics (its com_showFPS, the launcher's Performance overlay) on the HUD in VR
// (docs/VR_STEREO.md "Performance overlay").
//
// The game draws them into its GUI target, right-aligned in the top-right corner of the whole target (the
// first line 18 pixels from the top), not in the 16:9 band the HUD and menus are laid out in. The UI quad
// shows only that band (ETERNALVR_UI_CROP), so on a near-square eye target they were never seen. With
// ETERNALVR_PERF_OVERLAY=<level> (the level the launcher passed as +com_showFPS) the corner they take is
// shown on a quad of its own from the same swapchain, its bottom edge on the UI quad's top edge and its right
// edge on the UI quad's right edge, at the UI quad's scale (never smaller than at kReferenceWidth): just
// above the HUD panel's top-right corner, where it covers no part of the HUD.
//
// Pure: no Vulkan or OpenXR.

#include "ui_layer/ui_settings.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace evr::ui_layer {

// The game's Performance Metrics levels (its video menu's Performance Metrics, com_showFPS 0..3).
inline constexpr int kPerfOverlayMaxLevel = 3;

// The composition layers a frame may submit: OpenXR guarantees at least 16 (maxLayerCount).
inline constexpr std::uint32_t kMaxFrameLayers = 16;

// Whether the metrics' quad fits a frame that has `before` layers already and `after` more to come.
bool perfQuadFits(std::uint32_t before, std::uint32_t after);

// The level whose quad a frame shows: none while the GUI target holds a full-screen backdrop (a fade, as at a
// cinematic's start, or a menu's backdrop; the backdrop test of backdrop.hpp), which fills the corner too and
// would stand above the panel as an opaque rectangle; `level` otherwise.
int perfLevelShown(int level, bool fullScreenBackdrop);

// The band width (pixels) up to which the metrics' quad has the UI quad's scale; on a wider band it keeps the
// size it has at this width, so the text keeps its angular size on a larger render target.
inline constexpr std::uint32_t kReferenceWidth = 2048;

// ETERNALVR_PERF_OVERLAY: 0 (unset, the default) or the com_showFPS level 1..3. Anything else is 0 with a
// line in `warnings`.
int readPerfOverlayLevel(const EnvLookup& env, std::vector<std::string>& warnings);

// The corner of a `width` x `height` GUI target the game's metrics take at `level`, in pixels: the top-right
// corner, as wide and as tall as the level's lines are at their longest (the rig's GUI captures, at 2048 x
// 2176 and at 1448 x 1560: the game draws them in pixels of its own, the same size at every target size).
// nullopt for level 0 or an empty target.
std::optional<PixelRect> perfMetricsRect(int level, std::uint32_t width, std::uint32_t height);

// The metrics' own quad: the part of the GUI target it shows and where it sits, in metres from the UI quad's
// centre (+x right, +y up) on the UI quad's plane, for a UI quad `panelWidth` metres wide that shows `shown`
// of a `width` x `height` target. nullopt when there is nothing to add: level 0, an empty target or panel,
// or a corner the UI quad already shows (a target of 16:9, or ETERNALVR_UI_CROP=0).
struct PerfMetricsQuad {
    PixelRect source;
    float centreX = 0.0f;
    float centreY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};
std::optional<PerfMetricsQuad> perfMetricsQuad(
    int level, std::uint32_t width, std::uint32_t height, const PixelRect& shown, float panelWidth);

} // namespace evr::ui_layer
