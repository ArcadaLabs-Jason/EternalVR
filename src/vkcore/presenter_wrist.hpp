#pragma once

// The wrist HUD on the XR worker (ETERNALVR_HUD=wrist, docs/VR_HANDS_HUD.md): the UI quad split into its
// head-locked pieces and the quads on the off hand's wrist (presenter_wrist.cpp). XR worker only.

#include "ui_layer/ui_settings.hpp"
#include "ui_layer/wrist_hud.hpp"

#include <windows.h>

#include <openxr/openxr.h>

#include <array>
#include <cstdint>

namespace evr::vkcore {

class WristHud {
public:
    // The head-locked pieces (2 in practice) and the wrist quads (3) that replace the UI quad.
    static constexpr std::uint32_t kMaxQuads = 7;

    // XR_KHR_composition_layer_color_scale_bias is enabled (the quads fade); set at instance creation.
    bool colorScaleBias = false;

    // In wrist mode, the UI quad `ui` (showing part of a `width` x `height` GUI target) split into its
    // head-locked pieces and the wrist quads, written to `out` (at most kMaxQuads); 0 when the whole UI quad
    // stays as it is (panel mode, no controllers). The quads written point into this object (the fade), so
    // they are valid until the next call.
    std::uint32_t fill(const ui_layer::UiSettings& settings,
                       std::uint32_t width,
                       std::uint32_t height,
                       const XrCompositionLayerQuad& ui,
                       XrCompositionLayerQuad* out);

private:
    void logStats(const ui_layer::UiSettings& settings);

    ui_layer::WristFacing facing_;
    ui_layer::WristFade fade_;
    LONGLONG lastQpc_ = 0;
    std::uint64_t frames_ = 0;      // frames in wrist mode
    std::uint64_t shownFrames_ = 0; // of those, with the wrist quads shown
    bool logged_ = false;
    bool loggedFallback_ = false;
    bool loggedNoAbilities_ = false;
    float lastDegrees_ = 180.0f; // the last facing and gaze angles, for the log
    float lastGaze_ = 180.0f;
    std::array<XrCompositionLayerColorScaleBiasKHR, kMaxQuads> bias_{};
};

} // namespace evr::vkcore
