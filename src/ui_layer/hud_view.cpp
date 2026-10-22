#include "ui_layer/hud_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace evr::ui_layer {

namespace {

bool inside(const PixelRect& r, std::uint32_t width, std::uint32_t height) {
    return r.x >= 0 && r.y >= 0 && static_cast<std::int64_t>(r.x) + r.width <= width &&
           static_cast<std::int64_t>(r.y) + r.height <= height;
}

void appendf(
    std::string& s, const char* format, std::int64_t a, std::int64_t b, std::uint32_t c, std::uint32_t d) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), format, static_cast<long long>(a), static_cast<long long>(b), c, d);
    s += buf;
}

} // namespace

HudComposition
composeHudView(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height, const HudView& view) {
    const PixelRect& shown = view.shown;
    if (!rgba || shown.width == 0 || shown.height == 0 || !inside(shown, width, height) ||
        !(view.panelWidth > 0.0f)) {
        return {};
    }
    // Metres to pixels of the UI quad; the wheel's centre is straight ahead, the quad's centre panelCentreY
    // up, so in the UI quad's pixels (y down) the wheel's quad spans [x0, x0 + w) x [y0, y0 + h).
    const double perMetre = static_cast<double>(shown.width) / view.panelWidth;
    const bool wheel = view.wheel && inside(*view.wheel, width, height) && view.wheel->width != 0 &&
                       view.wheel->height != 0 && view.wheelWidth > 0.0f && view.wheelHeight > 0.0f;
    const double w = wheel ? view.wheelWidth * perMetre : 0.0;
    const double h = wheel ? view.wheelHeight * perMetre : 0.0;
    const double x0 = shown.width / 2.0 - w / 2.0;
    const double y0 = shown.height / 2.0 + view.panelCentreY * perMetre - h / 2.0;
    // The canvas: the UI quad's area and the wheel's quad.
    double left = 0.0;
    double top = 0.0;
    double right = shown.width;
    double bottom = shown.height;
    if (wheel) {
        left = std::min(left, std::floor(x0));
        top = std::min(top, std::floor(y0));
        right = std::max(right, std::ceil(x0 + w));
        bottom = std::max(bottom, std::ceil(y0 + h));
    }
    // A quad far outside the HUD (a broken pose) is not drawn into a huge image.
    if (right - left > 4.0 * shown.width || bottom - top > 4.0 * shown.height) {
        return {};
    }
    HudComposition c;
    c.panelX = static_cast<std::uint32_t>(-left);
    c.panelY = static_cast<std::uint32_t>(-top);
    c.image.width = static_cast<std::uint32_t>(right - left);
    c.image.height = static_cast<std::uint32_t>(bottom - top);
    const std::uint32_t outW = c.image.width;
    c.image.rgba.assign(std::size_t{outW} * c.image.height * 4, 0);
    std::uint8_t* const out = c.image.rgba.data();
    for (const PixelRect& p : view.pieces) {
        if (!inside(p, width, height) || p.x < shown.x || p.y < shown.y ||
            static_cast<std::int64_t>(p.x) + p.width > static_cast<std::int64_t>(shown.x) + shown.width ||
            static_cast<std::int64_t>(p.y) + p.height > static_cast<std::int64_t>(shown.y) + shown.height) {
            continue;
        }
        for (std::uint32_t y = 0; y < p.height; ++y) {
            const std::uint8_t* src = rgba + (std::size_t{p.y + y} * width + p.x) * 4;
            std::uint8_t* dst =
                out + (std::size_t{c.panelY + p.y - shown.y + y} * outW + c.panelX + (p.x - shown.x)) * 4;
            std::copy(src, src + std::size_t{p.width} * 4, dst);
        }
    }
    if (!wheel) {
        return c;
    }
    const PixelRect& r = *view.wheel;
    for (std::uint32_t y = 0; y < c.image.height; ++y) {
        const double v = (static_cast<double>(y) - c.panelY + 0.5 - y0) / h;
        if (v < 0.0 || v >= 1.0) {
            continue;
        }
        const auto sy = r.y + static_cast<std::int64_t>(v * r.height);
        for (std::uint32_t x = 0; x < outW; ++x) {
            const double u = (static_cast<double>(x) - c.panelX + 0.5 - x0) / w;
            if (u < 0.0 || u >= 1.0) {
                continue;
            }
            const auto sx = r.x + static_cast<std::int64_t>(u * r.width);
            const std::uint8_t* src =
                rgba + (static_cast<std::size_t>(sy) * width + static_cast<std::size_t>(sx)) * 4;
            std::uint8_t* dst = out + (std::size_t{y} * outW + x) * 4;
            // Premultiplied "over".
            const int keep = 255 - src[3];
            for (int k = 0; k < 4; ++k) {
                dst[k] = static_cast<std::uint8_t>(std::min(255, src[k] + (dst[k] * keep + 127) / 255));
            }
        }
    }
    return c;
}

std::string describeHudView(const HudView& view) {
    std::string s;
    char buf[160];
    std::snprintf(
        buf, sizeof(buf),
        "hud as composed (head-locked quads, drawn in -HUD.png): UI quad %.2f m wide, centre %.2f m up, "
        "GUI rect",
        view.panelWidth, view.panelCentreY);
    s += buf;
    appendf(s, " (%lld, %lld) %u x %u; ", view.shown.x, view.shown.y, view.shown.width, view.shown.height);
    s += std::to_string(view.pieces.size()) + " piece(s):";
    for (const PixelRect& p : view.pieces) {
        appendf(s, " (%lld, %lld) %u x %u", p.x, p.y, p.width, p.height);
    }
    if (view.wheel) {
        appendf(s, "; weapon wheel quad straight ahead: GUI rect (%lld, %lld) %u x %u", view.wheel->x,
                view.wheel->y, view.wheel->width, view.wheel->height);
        std::snprintf(buf, sizeof(buf), ", %.3f x %.3f m", view.wheelWidth, view.wheelHeight);
        s += buf;
    } else {
        s += "; no weapon wheel quad";
    }
    if (view.handQuads) {
        s += "; hand HUD quads shown too (not drawn)";
    }
    return s + "\n";
}

} // namespace evr::ui_layer
