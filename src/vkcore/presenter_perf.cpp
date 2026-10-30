#include "vkcore/presenter_perf.hpp"

#include "ui_layer/perf_metrics.hpp"
#include "vkcore/log.hpp"

#include <optional>

namespace evr::vkcore {

bool fillPerfMetricsQuad(int level,
                         std::uint32_t width,
                         std::uint32_t height,
                         const XrCompositionLayerQuad& ui,
                         std::uint32_t layersBefore,
                         std::uint32_t layersAfter,
                         XrCompositionLayerQuad& out) {
    const XrRect2Di& shownRect = ui.subImage.imageRect;
    const ui_layer::PixelRect shown{shownRect.offset.x, shownRect.offset.y,
                                    static_cast<std::uint32_t>(shownRect.extent.width),
                                    static_cast<std::uint32_t>(shownRect.extent.height)};
    const std::optional<ui_layer::PerfMetricsQuad> q =
        ui_layer::perfMetricsQuad(level, width, height, shown, ui.size.width);
    if (!q) {
        return false;
    }
    if (!ui_layer::perfQuadFits(layersBefore, layersAfter)) {
        static bool loggedFull = false; // XR worker only
        if (!loggedFull) {
            loggedFull = true;
            EVR_LOG("ui: the game's Performance Metrics quad is left out of a frame with %u layer(s) and up "
                    "to %u "
                    "more: OpenXR guarantees %u",
                    layersBefore, layersAfter, ui_layer::kMaxFrameLayers);
        }
        return false;
    }
    out = ui;
    out.next = nullptr;
    out.subImage.imageRect = {
        {q->source.x, q->source.y},
        {static_cast<std::int32_t>(q->source.width), static_cast<std::int32_t>(q->source.height)}};
    out.pose.position.x += q->centreX;
    out.pose.position.y += q->centreY;
    out.size = {q->width, q->height};
    static bool logged = false; // XR worker only
    if (!logged) {
        logged = true;
        EVR_LOG("ui: the game's Performance Metrics (com_showFPS %d) on their own quad above the HUD panel's "
                "top-right corner: GUI rect (%d, %d) %u x %u of %u x %u, %.3f x %.3f m",
                level, q->source.x, q->source.y, q->source.width, q->source.height, width, height,
                static_cast<double>(q->width), static_cast<double>(q->height));
    }
    return true;
}

} // namespace evr::vkcore
