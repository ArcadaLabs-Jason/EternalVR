#pragma once

// The weapon wheel in front of the eyes (docs/VR_MENUS.md, "The weapon wheel").
//
// The game draws its weapon wheel (idSWFWidget_WeaponWheel) to the right of the middle of the HUD's 16:9
// band: its centre at 79.7% of the band's width and 48% of its height, its diameter 28% of the band's width,
// measured on the rig's GUI captures at 2048x2208, 1760x1400 and 1440x1600 (the same band coordinates to
// 0.001). On the head-locked quad that puts it about 22 degrees right of straight ahead on the default HUD
// (2.0 m wide at 1.5 m) and about 40 degrees right on a 2.5 m HUD at 0.9 m: at the edge of the view or past
// it.
//
// While the wheel is up its part of the GUI image goes on a quad of its own, straight ahead at the HUD's
// distance, at the size the wheel has on the default HUD; the head-locked quad keeps the rest of the band,
// cut into rectangles around it (as for the blocks the hand HUDs move), so the wheel is never shown twice and
// nothing else on the HUD moves.

#include "ui_layer/hud_regions.hpp"
#include "ui_layer/ui_settings.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace evr::ui_layer {

// The wheel's rectangle in the band (BandRect coordinates): the measured wheel (x 0.655..0.938, y
// 0.228..0.730 with the highlighted segment's bar and the top tab) with a margin of at least 1% of the band's
// width, clear of the corner blocks below it (from y 0.76) and of the crosshair's rings (up to x 0.61).
BandRect wheelBandRect();

// The wheel's quad while it is centred: the part of the target it shows and its size in metres. Its centre is
// straight ahead of the eyes, `distance` metres away (x and y 0, z = -distance in the head's space).
struct CentredWheel {
    PixelRect rect;
    float distance = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// The wheel's quad for a `width` x `height` target at `distanceMetres`: wheelBandRect in pixels, as wide as
// the angle it spans on the default HUD (UiSettings' width and distance) seen straight on. nullopt for an
// empty target or a distance that is not positive.
std::optional<CentredWheel> centredWheel(std::uint32_t width, std::uint32_t height, float distanceMetres);

// The head-locked HUD of one frame: the rectangles of the target the head-locked quad keeps (`shown`, the UI
// quad's image rectangle, minus the cut rectangles of the blocks in `moved` and, with `wheelCentred`, the
// wheel's rectangle) and, with `wheelCentred`, the wheel's own quad.
struct HeadLockedHud {
    std::vector<PixelRect> pieces;
    std::optional<CentredWheel> wheel;
};
HeadLockedHud headLockedHud(const PixelRect& shown,
                            std::uint32_t width,
                            std::uint32_t height,
                            const std::vector<WristBlock>& moved,
                            bool wheelCentred,
                            float distanceMetres);

// When the wheel is centred. The frame of the GUI image on the quad says whether the wheel was up then (its
// view record, from the forced-view gate's weapon wheel state at the camera hook), so the centred quad
// follows the image the headset shows, not the game's latest frame. It stays for `holdSeconds` after the last
// such image, so a frame of the image and the game's state disagreeing at the close does not flash the wheel
// at its place on the HUD.
class WheelCentring {
public:
    static constexpr double kHoldSeconds = 0.1;

    struct Step {
        bool centred = false;
        bool started = false; // centred from this frame
        bool ended = false;   // back on the HUD from this frame
    };
    // One headset frame at `seconds`: `wheelInImage`, the frame of the shown GUI image had the wheel up.
    Step update(bool wheelInImage, double seconds);

private:
    bool centred_ = false;
    double lastUp_ = 0.0;
};

} // namespace evr::ui_layer
