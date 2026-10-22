#pragma once

// The head-locked HUD as the headset composes it, for the in-headset capture (vkcore/bug_capture.hpp): the
// GUI image alone (-UI.png) does not show what the player sees once the hand HUDs and the centred weapon
// wheel cut it into quads. The XR worker keeps the last frame's composition (the UI quad's pieces and the
// wheel's quad); the capture draws it as -HUD.png and describes it in its text file.
//
// Pure: no Vulkan or OpenXR.

#include "ui_layer/ui_settings.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace evr::ui_layer {

struct HudView {
    PixelRect shown;               // the UI quad's image rectangle (the band)
    float panelWidth = 0.0f;       // the UI quad's width, metres
    float panelCentreY = 0.0f;     // the UI quad's centre above the eyes, metres (view space, +y up)
    std::vector<PixelRect> pieces; // the head-locked parts of `shown` (all of it while it is not cut)
    // The weapon wheel's quad straight ahead (x 0, y 0), at the HUD's distance, and its size in metres.
    std::optional<PixelRect> wheel;
    float wheelWidth = 0.0f;
    float wheelHeight = 0.0f;
    bool handQuads = false; // the wrist or weapon HUD's quads were shown too (not drawn: they follow a hand)
};

// An RGBA8 image (premultiplied), `width` x `height`.
struct HudImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

// The head-locked quads of `view` drawn from the `width` x `height` RGBA8 GUI image `rgba` (premultiplied):
// the UI quad's area at the GUI image's scale (shown.width x shown.height pixels, at `panelX`, `panelY`) with
// the pieces where the UI quad shows them, and the wheel's quad over them where it is seen (all quads share
// the HUD's distance, so the angles match); the image grows past the UI quad's area to hold all of the
// wheel's quad. Empty for an image or view that does not fit.
struct HudComposition {
    HudImage image;
    std::uint32_t panelX = 0;
    std::uint32_t panelY = 0;
};
HudComposition
composeHudView(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height, const HudView& view);

// One line for the capture's text file: the UI quad, its pieces and the wheel's quad.
std::string describeHudView(const HudView& view);

} // namespace evr::ui_layer
