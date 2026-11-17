#include "ui_layer/gun_crosshair.hpp"

#include "ui_layer/hud_regions.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace evr::ui_layer {

namespace {

std::int64_t right(const PixelRect& r) {
    return static_cast<std::int64_t>(r.x) + r.width;
}

std::int64_t bottom(const PixelRect& r) {
    return static_cast<std::int64_t>(r.y) + r.height;
}

// The part of `a` inside `b`; nullopt when they do not overlap.
std::optional<PixelRect> overlap(const PixelRect& a, const PixelRect& b) {
    const std::int64_t x0 = std::max<std::int64_t>(a.x, b.x);
    const std::int64_t y0 = std::max<std::int64_t>(a.y, b.y);
    const std::int64_t x1 = std::min(right(a), right(b));
    const std::int64_t y1 = std::min(bottom(a), bottom(b));
    if (x1 <= x0 || y1 <= y0) {
        return std::nullopt;
    }
    return PixelRect{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0),
                     static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)};
}

// A size of `fraction` of the band's height in whole pixels, at most `limit`.
std::uint32_t sideOf(float fraction, std::uint32_t bandHeight, std::uint32_t limit) {
    const auto pixels = static_cast<std::uint32_t>(std::lround(fraction * static_cast<float>(bandHeight)));
    return std::min(pixels, limit);
}

} // namespace

PixelRect gunCrosshairRect(std::uint32_t width, std::uint32_t height) {
    const PixelRect band = wideContentRect(width, height);
    if (band.width == 0 || band.height == 0) {
        return {};
    }
    const std::uint32_t w = sideOf(kGunCrosshairWidth, band.height, band.width);
    const std::uint32_t h = sideOf(kGunCrosshairHeight, band.height, band.height);
    return {band.x + static_cast<std::int32_t>((band.width - w) / 2),
            band.y + static_cast<std::int32_t>((band.height - h) / 2), w, h};
}

std::optional<std::array<PixelPoint, kBackdropProbePoints>> gunCrosshairBorderPoints(const PixelRect& rect,
                                                                                     std::uint32_t width,
                                                                                     std::uint32_t height,
                                                                                     std::uint32_t margin) {
    if (rect.width == 0 || rect.height == 0 || width == 0 || height == 0) {
        return std::nullopt;
    }
    const auto clampTo = [](std::int64_t v, std::uint32_t size) {
        return static_cast<std::uint32_t>(
            std::clamp<std::int64_t>(v, 0, static_cast<std::int64_t>(size) - 1));
    };
    const std::int64_t m = margin;
    const std::uint32_t x0 = clampTo(rect.x - m, width);
    const std::uint32_t x1 = clampTo(right(rect) - 1 + m, width);
    const std::uint32_t xm = clampTo(rect.x + static_cast<std::int64_t>(rect.width / 2), width);
    const std::uint32_t y0 = clampTo(rect.y - m, height);
    const std::uint32_t y1 = clampTo(bottom(rect) - 1 + m, height);
    const std::uint32_t ym = clampTo(rect.y + static_cast<std::int64_t>(rect.height / 2), height);
    return std::array<PixelPoint, kBackdropProbePoints>{
        PixelPoint{x0, y0}, PixelPoint{xm, y0}, PixelPoint{x1, y0}, PixelPoint{x0, ym},
        PixelPoint{x1, ym}, PixelPoint{x0, y1}, PixelPoint{xm, y1}, PixelPoint{x1, y1}};
}

GunCrosshairCuts gunCrosshairCuts(const PixelRect& rect, const std::vector<PixelRect>& markerBoxes) {
    GunCrosshairCuts out;
    if (rect.width == 0 || rect.height == 0) {
        return out;
    }
    std::vector<PixelRect> inside;
    for (const PixelRect& box : markerBoxes) {
        if (const std::optional<PixelRect> part = overlap(box, rect)) {
            inside.push_back(*part);
            out.moved.push_back({part->x - rect.x, part->y - rect.y, part->width, part->height});
        }
    }
    out.hud = inside.empty() ? std::vector<PixelRect>{rect} : subtractRects(rect, inside);
    return out;
}

GunCrosshairSize gunCrosshairSize(const PixelRect& rect,
                                  std::uint32_t shownWidth,
                                  float panelWidthMetres,
                                  float panelDistanceMetres,
                                  float atMetres) {
    if (rect.width == 0 || rect.height == 0 || shownWidth == 0 || !std::isfinite(panelWidthMetres) ||
        !std::isfinite(panelDistanceMetres) || !std::isfinite(atMetres) || !(panelWidthMetres > 0.0f) ||
        !(panelDistanceMetres > 0.0f) || !(atMetres > 0.0f)) {
        return {};
    }
    // A pixel's size on the HUD quad, carried along its angle to `atMetres`.
    const float metresPerPixel =
        panelWidthMetres / static_cast<float>(shownWidth) * (atMetres / panelDistanceMetres);
    return {static_cast<float>(rect.width) * metresPerPixel,
            static_cast<float>(rect.height) * metresPerPixel};
}

} // namespace evr::ui_layer
