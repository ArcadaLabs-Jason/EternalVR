#include "ui_layer/marker_boxes.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::ui_layer {

namespace {

struct Box {
    std::int64_t left = 0;
    std::int64_t top = 0;
    std::int64_t right = 0; // exclusive
    std::int64_t bottom = 0;
};

bool overlap(const Box& a, const Box& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

Box limitOf(const PixelRect& band) {
    return {band.x, band.y, band.x + static_cast<std::int64_t>(band.width),
            band.y + static_cast<std::int64_t>(band.height)};
}

// The spot's box clamped to the band; nullopt for a spot outside the band or a box left empty.
std::optional<Box> spotBox(const MarkerSpot& s, const PixelRect& band) {
    if (!(s.x >= 0.0f && s.x <= 1.0f && s.y >= 0.0f && s.y <= 1.0f)) {
        return std::nullopt;
    }
    const auto w = static_cast<double>(band.width);
    const auto h = static_cast<double>(band.height);
    const Box limit = limitOf(band);
    const double x = band.x + s.x * w;
    const double y = band.y + s.y * h;
    Box b{static_cast<std::int64_t>(std::floor(x - kMarkerHalfWidth * w)),
          static_cast<std::int64_t>(std::floor(y - kMarkerAbove * h)),
          static_cast<std::int64_t>(std::ceil(x + kMarkerHalfWidth * w)),
          static_cast<std::int64_t>(std::ceil(y + kMarkerBelow * h))};
    b.left = std::max(b.left, limit.left);
    b.top = std::max(b.top, limit.top);
    b.right = std::min(b.right, limit.right);
    b.bottom = std::min(b.bottom, limit.bottom);
    if (b.left >= b.right || b.top >= b.bottom) {
        return std::nullopt;
    }
    return b;
}

PixelRect toRect(const Box& b) {
    return {static_cast<std::int32_t>(b.left), static_cast<std::int32_t>(b.top),
            static_cast<std::uint32_t>(b.right - b.left), static_cast<std::uint32_t>(b.bottom - b.top)};
}

} // namespace

std::vector<PixelRect> markerBoxes(const std::vector<MarkerSpot>& spots, const PixelRect& band) {
    std::vector<Box> boxes;
    if (band.width == 0 || band.height == 0) {
        return {};
    }
    for (const MarkerSpot& s : spots) {
        if (const std::optional<Box> b = spotBox(s, band)) {
            boxes.push_back(*b);
        }
    }
    // Merging two boxes can make the merged one reach a third: go again until a pass merges nothing.
    for (bool merged = true; merged;) {
        merged = false;
        for (std::size_t i = 0; i < boxes.size() && !merged; ++i) {
            for (std::size_t j = i + 1; j < boxes.size(); ++j) {
                if (overlap(boxes[i], boxes[j])) {
                    boxes[i] = {std::min(boxes[i].left, boxes[j].left), std::min(boxes[i].top, boxes[j].top),
                                std::max(boxes[i].right, boxes[j].right),
                                std::max(boxes[i].bottom, boxes[j].bottom)};
                    boxes.erase(boxes.begin() + static_cast<std::ptrdiff_t>(j));
                    merged = true;
                    break;
                }
            }
        }
    }
    std::vector<PixelRect> out;
    out.reserve(boxes.size());
    for (const Box& b : boxes) {
        out.push_back(toRect(b));
    }
    return out;
}

float markerShiftPixels(const MarkerParallax& parallax, const MarkerEye& eye, float ahead) {
    if (!(ahead > 0.0f) || !std::isfinite(ahead) || !(parallax.quadMetres > 0.0f) ||
        !(parallax.pixelsPerMetre > 0.0f) || !(parallax.unitsPerMetre > 0.0f) ||
        !std::isfinite(eye.shownMetres) || !std::isfinite(eye.renderedMetres)) {
        return 0.0f;
    }
    const float target = std::max(ahead / parallax.unitsPerMetre, kMarkerNearestMetres);
    return (eye.shownMetres - eye.renderedMetres * parallax.quadMetres / target) * parallax.pixelsPerMetre;
}

std::vector<MarkerCopy> markerEyeCopies(const std::vector<MarkerSpot>& spots,
                                        const PixelRect& band,
                                        const MarkerParallax& parallax,
                                        const MarkerEye& eye) {
    std::vector<MarkerCopy> out;
    if (band.width == 0 || band.height == 0) {
        return out;
    }
    const Box limit = limitOf(band);
    for (const MarkerSpot& s : spots) {
        std::optional<Box> b = spotBox(s, band);
        if (!b) {
            continue;
        }
        const auto dx = static_cast<std::int64_t>(std::lround(markerShiftPixels(parallax, eye, s.ahead)));
        // Trim what would land outside the band once moved.
        b->left = std::max(b->left, limit.left - dx);
        b->right = std::min(b->right, limit.right - dx);
        if (b->left >= b->right) {
            continue;
        }
        out.push_back({toRect(*b), static_cast<std::int32_t>(dx)});
    }
    return out;
}

} // namespace evr::ui_layer
