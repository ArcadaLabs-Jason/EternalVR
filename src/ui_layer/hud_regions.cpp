#include "ui_layer/hud_regions.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace evr::ui_layer {

namespace {

std::int64_t right(const PixelRect& r) {
    return static_cast<std::int64_t>(r.x) + r.width;
}

std::int64_t bottom(const PixelRect& r) {
    return static_cast<std::int64_t>(r.y) + r.height;
}

PixelRect fromEdges(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
    if (x1 <= x0 || y1 <= y0) {
        return {};
    }
    return {static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0), static_cast<std::uint32_t>(x1 - x0),
            static_cast<std::uint32_t>(y1 - y0)};
}

} // namespace

BandRect wristBlockRect(WristBlock block) {
    switch (block) {
    case WristBlock::Vitals:
        return {0.0f, 0.76f, 0.215f, 1.0f};
    case WristBlock::Weapon:
        return {0.765f, 0.76f, 1.0f, 1.0f};
    case WristBlock::Abilities:
        return {0.39f, 0.43f, 0.61f, 0.59f};
    }
    return {};
}

PixelRect toPixels(const BandRect& rect, std::uint32_t width, std::uint32_t height) {
    // The same band the UI quad shows (presenter_ui.cpp), so a crop lines up with it to the pixel.
    const PixelRect band = wideContentRect(width, height);
    const auto bx = static_cast<double>(band.x);
    const auto by = static_cast<double>(band.y);
    const auto bw = static_cast<double>(band.width);
    const auto bh = static_cast<double>(band.height);
    const auto clampX = [&band](double v) {
        return std::clamp<std::int64_t>(static_cast<std::int64_t>(v), band.x, right(band));
    };
    const auto clampY = [&band](double v) {
        return std::clamp<std::int64_t>(static_cast<std::int64_t>(v), band.y, bottom(band));
    };
    const std::int64_t x0 = clampX(std::floor(bx + rect.x0 * bw));
    const std::int64_t x1 = clampX(std::ceil(bx + rect.x1 * bw));
    const std::int64_t y0 = clampY(std::floor(by + rect.y0 * bh));
    const std::int64_t y1 = clampY(std::ceil(by + rect.y1 * bh));
    return fromEdges(x0, y0, x1, y1);
}

PixelRect cutRect(WristBlock block, std::uint32_t width, std::uint32_t height) {
    const PixelRect r = toPixels(wristBlockRect(block), width, height);
    if (r.width == 0) {
        return {};
    }
    const PixelRect band = wideContentRect(width, height);
    switch (block) {
    case WristBlock::Vitals:
        return fromEdges(band.x, r.y, right(r), bottom(band));
    case WristBlock::Weapon:
        return fromEdges(r.x, r.y, right(band), bottom(band));
    case WristBlock::Abilities:
        return {};
    }
    return {};
}

std::vector<PixelRect> subtractRects(const PixelRect& whole, const std::vector<PixelRect>& cuts) {
    if (whole.width == 0 || whole.height == 0) {
        return {};
    }
    const std::int64_t top = whole.y;
    const std::int64_t base = bottom(whole);
    std::vector<std::int64_t> ys{top, base};
    for (const PixelRect& c : cuts) {
        if (c.width != 0 && c.height != 0) {
            ys.push_back(std::clamp<std::int64_t>(c.y, top, base));
            ys.push_back(std::clamp<std::int64_t>(bottom(c), top, base));
        }
    }
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());

    // Per row band: the x intervals left after the cuts that cover the band.
    using Intervals = std::vector<std::pair<std::int64_t, std::int64_t>>;
    std::vector<PixelRect> out;
    Intervals previous;
    std::vector<std::size_t> previousIndex; // out[] index of each interval of the previous band
    for (std::size_t i = 0; i + 1 < ys.size(); ++i) {
        const std::int64_t y0 = ys[i];
        const std::int64_t y1 = ys[i + 1];
        Intervals covered;
        for (const PixelRect& c : cuts) {
            if (c.width != 0 && c.height != 0 && c.y <= y0 && bottom(c) >= y1) {
                covered.emplace_back(std::max<std::int64_t>(c.x, whole.x), std::min(right(c), right(whole)));
            }
        }
        std::sort(covered.begin(), covered.end());
        Intervals free;
        std::int64_t x = whole.x;
        for (const auto& [c0, c1] : covered) {
            if (c0 > x) {
                free.emplace_back(x, c0);
            }
            x = std::max(x, c1);
        }
        if (x < right(whole)) {
            free.emplace_back(x, right(whole));
        }
        std::vector<std::size_t> index;
        if (free == previous) {
            // The same columns as the band above: grow those rectangles down.
            for (const std::size_t k : previousIndex) {
                out[k].height += static_cast<std::uint32_t>(y1 - y0);
            }
            index = previousIndex;
        } else {
            for (const auto& [x0, x1] : free) {
                index.push_back(out.size());
                out.push_back(fromEdges(x0, y0, x1, y1));
            }
        }
        previous = std::move(free);
        previousIndex = std::move(index);
    }
    return out;
}

PanelPiece panelPiece(const PixelRect& rect, const PixelRect& shown, float panelWidth) {
    if (shown.width == 0 || shown.height == 0) {
        return {};
    }
    const float metresPerPixel = panelWidth / static_cast<float>(shown.width);
    const float cx = static_cast<float>(rect.x) + static_cast<float>(rect.width) / 2.0f;
    const float cy = static_cast<float>(rect.y) + static_cast<float>(rect.height) / 2.0f;
    const float shownX = static_cast<float>(shown.x) + static_cast<float>(shown.width) / 2.0f;
    const float shownY = static_cast<float>(shown.y) + static_cast<float>(shown.height) / 2.0f;
    return {(cx - shownX) * metresPerPixel, (shownY - cy) * metresPerPixel,
            static_cast<float>(rect.width) * metresPerPixel,
            static_cast<float>(rect.height) * metresPerPixel};
}

} // namespace evr::ui_layer
