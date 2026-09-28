#include "ui_layer/backdrop.hpp"

#include <algorithm>
#include <cmath>

namespace evr::ui_layer {

std::optional<std::array<PixelPoint, kBackdropProbePoints>>
backdropProbePoints(std::uint32_t width, std::uint32_t height, float maskFraction, std::uint32_t margin) {
    // The square as copyRegionsWithoutCentre cuts it.
    const auto side =
        static_cast<std::uint32_t>(std::lround(std::max(0.0f, maskFraction) * static_cast<float>(height)));
    if (side == 0 || side >= width || side >= height) {
        return std::nullopt;
    }
    const std::uint32_t left = (width - side) / 2;
    const std::uint32_t top = (height - side) / 2;
    // `margin` pixels between the square and the points on every side.
    const std::uint64_t right = std::uint64_t{left} + side + margin;
    const std::uint64_t bottom = std::uint64_t{top} + side + margin;
    if (left < margin + 1 || top < margin + 1 || right >= width || bottom >= height) {
        return std::nullopt;
    }
    const std::uint32_t x0 = left - margin - 1;
    const std::uint32_t y0 = top - margin - 1;
    const auto x1 = static_cast<std::uint32_t>(right);
    const auto y1 = static_cast<std::uint32_t>(bottom);
    const std::uint32_t cx = left + side / 2;
    const std::uint32_t cy = top + side / 2;
    return std::array<PixelPoint, kBackdropProbePoints>{{
        {x0, y0},
        {cx, y0},
        {x1, y0},
        {x1, cy},
        {x1, y1},
        {cx, y1},
        {x0, y1},
        {x0, cy},
    }};
}

bool readingShowsBackdrop(const std::array<std::uint8_t, kBackdropProbePoints * 4>& rgba) {
    std::size_t opaque = 0;
    for (std::size_t i = 0; i < kBackdropProbePoints; ++i) {
        if (rgba[i * 4 + 3] >= kBackdropMinAlpha) {
            ++opaque;
        }
    }
    return opaque >= kBackdropMinPoints;
}

bool BackdropDetector::update(bool showsBackdrop) {
    if (showsBackdrop == backdrop_) {
        run_ = 0;
        return false;
    }
    if (++run_ < (backdrop_ ? kOffReadings : kOnReadings)) {
        return false;
    }
    backdrop_ = showsBackdrop;
    run_ = 0;
    return true;
}

void BackdropDetector::reset() {
    backdrop_ = false;
    run_ = 0;
}

} // namespace evr::ui_layer
