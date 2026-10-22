#include "ui_layer/weapon_wheel.hpp"

#include <cmath>

namespace evr::ui_layer {

std::optional<CentredWheel>
centredWheel(const PixelRect& rect, std::uint32_t width, std::uint32_t height, float distanceMetres) {
    if (!(distanceMetres > 0.0f) || !std::isfinite(distanceMetres)) {
        return std::nullopt;
    }
    const PixelRect band = wideContentRect(width, height);
    if (rect.width == 0 || rect.height == 0 || band.width == 0) {
        return std::nullopt;
    }
    // On the default HUD the band is UiSettings{}.widthMetres wide at UiSettings{}.distanceMetres; the same
    // angle at another distance scales with it.
    const UiSettings defaults;
    const float metresPerPixel =
        defaults.widthMetres / static_cast<float>(band.width) * distanceMetres / defaults.distanceMetres;
    CentredWheel wheel;
    wheel.rect = rect;
    wheel.distance = distanceMetres;
    wheel.width = static_cast<float>(rect.width) * metresPerPixel;
    wheel.height = static_cast<float>(rect.height) * metresPerPixel;
    return wheel;
}

HeadLockedHud headLockedHud(const PixelRect& shown,
                            std::uint32_t width,
                            std::uint32_t height,
                            const std::vector<PixelRect>& cuts,
                            const std::optional<PixelRect>& wheel,
                            float distanceMetres) {
    HeadLockedHud hud;
    if (width == 0 || height == 0) {
        return hud;
    }
    std::vector<PixelRect> all = cuts;
    if (wheel) {
        hud.wheel = centredWheel(*wheel, width, height, distanceMetres);
        if (hud.wheel) {
            all.push_back(hud.wheel->rect);
        }
    }
    hud.pieces = subtractRects(shown, all);
    return hud;
}

WheelCentring::Step WheelCentring::update(bool wheelInImage, double seconds) {
    Step step;
    if (wheelInImage) {
        lastUp_ = seconds;
    }
    const bool centred = wheelInImage || (centred_ && seconds - lastUp_ < kHoldSeconds);
    step.started = centred && !centred_;
    step.ended = !centred && centred_;
    centred_ = centred;
    step.centred = centred;
    return step;
}

} // namespace evr::ui_layer
