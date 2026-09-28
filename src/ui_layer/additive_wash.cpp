#include "ui_layer/additive_wash.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace evr::ui_layer {

void removeAdditiveWash(std::span<std::uint8_t> rgba, std::uint32_t width, std::uint32_t height) {
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    if (pixels == 0 || rgba.size() < pixels * 4) {
        return;
    }
    const auto excess = [&](std::size_t p, int c) {
        const int a = rgba[p * 4 + 3];
        return static_cast<std::uint8_t>(std::max(rgba[p * 4 + static_cast<std::size_t>(c)] - a, 0));
    };
    const std::uint32_t bw = washBlocks(width);
    const std::uint32_t bh = washBlocks(height);
    using Rgb = std::array<std::uint8_t, 3>;
    std::vector<Rgb> floors(static_cast<std::size_t>(bw) * bh, Rgb{255, 255, 255});
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t p = static_cast<std::size_t>(y) * width + x;
            Rgb& f = floors[static_cast<std::size_t>(y / kWashBlock) * bw + x / kWashBlock];
            for (int c = 0; c < 3; ++c) {
                f[static_cast<std::size_t>(c)] = std::min(f[static_cast<std::size_t>(c)], excess(p, c));
            }
        }
    }
    std::vector<Rgb> wash(floors.size(), Rgb{0, 0, 0});
    for (std::uint32_t by = 0; by < bh; ++by) {
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
            Rgb& w = wash[static_cast<std::size_t>(by) * bw + bx];
            for (std::uint32_t ny = by > 0 ? by - 1 : 0; ny <= std::min(by + 1, bh - 1); ++ny) {
                for (std::uint32_t nx = bx > 0 ? bx - 1 : 0; nx <= std::min(bx + 1, bw - 1); ++nx) {
                    const Rgb& f = floors[static_cast<std::size_t>(ny) * bw + nx];
                    for (std::size_t c = 0; c < 3; ++c) {
                        w[c] = std::max(w[c], f[c]);
                    }
                }
            }
        }
    }
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t p = static_cast<std::size_t>(y) * width + x;
            const Rgb& w = wash[static_cast<std::size_t>(y / kWashBlock) * bw + x / kWashBlock];
            for (int c = 0; c < 3; ++c) {
                const std::uint8_t e = excess(p, c);
                rgba[p * 4 + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                    rgba[p * 4 + static_cast<std::size_t>(c)] - std::min(w[static_cast<std::size_t>(c)], e));
            }
        }
    }
}

} // namespace evr::ui_layer
