#include "ui_layer/additive_wash.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace evr::ui_layer {

namespace {

using Rgb = std::array<std::uint8_t, 3>;

struct Floor {
    Rgb light{255, 255, 255};
    bool known = false; // the block has an uncovered (alpha 0) pixel
};

// Where pixel `x` sits between block centres along one axis of `blocks` blocks: the nearer centre at or
// before it (clamped to the first and last block) and its distance past that centre, 0 to kWashSpan.
struct Between {
    std::uint32_t first;
    std::uint32_t second;
    std::uint32_t past;
};

Between between(std::uint32_t x, std::uint32_t blocks) {
    const std::uint32_t halfPixels = 2 * x + 1; // the pixel's centre; block i's centre is at (2i + 1) * block
    if (halfPixels < kWashBlock) {
        return {0, 0, 0};
    }
    const std::uint32_t first = (halfPixels - kWashBlock) / kWashSpan;
    if (first + 1 >= blocks) {
        return {blocks - 1, blocks - 1, 0};
    }
    return {first, first + 1, halfPixels - kWashBlock - first * kWashSpan};
}

} // namespace

std::uint8_t washBound(std::uint8_t colour, std::uint8_t alpha) {
    if (alpha == 255) {
        return 255;
    }
    // (colour + 1/2) / (1 - alpha) in whole numbers: colour is the light rounded to a byte.
    return static_cast<std::uint8_t>(
        std::min<std::uint32_t>(255, (2u * colour + 1u) * 255u / (2u * (255u - alpha))));
}

std::uint8_t washShare(std::uint8_t wash, std::uint8_t alpha) {
    return static_cast<std::uint8_t>((wash * (255u - alpha) + 127u) / 255u);
}

void removeAdditiveWash(std::span<std::uint8_t> rgba, std::uint32_t width, std::uint32_t height) {
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    if (pixels == 0 || rgba.size() < pixels * 4) {
        return;
    }
    const std::uint32_t bw = washBlocks(width);
    const std::uint32_t bh = washBlocks(height);
    std::vector<Floor> floors(static_cast<std::size_t>(bw) * bh);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint8_t* p = &rgba[(static_cast<std::size_t>(y) * width + x) * 4];
            Floor& f = floors[static_cast<std::size_t>(y / kWashBlock) * bw + x / kWashBlock];
            for (std::size_t c = 0; c < 3; ++c) {
                f.light[c] = std::min(f.light[c], washBound(p[c], p[3]));
            }
            f.known = f.known || p[3] == 0;
        }
    }
    std::vector<Rgb> levels(floors.size(), Rgb{0, 0, 0});
    for (std::uint32_t by = 0; by < bh; ++by) {
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
            std::array<int, 3> lit{};
            std::array<int, 3> unlit{};
            Rgb most{0, 0, 0};
            for (std::uint32_t ny = by > 0 ? by - 1 : 0; ny <= std::min(by + 1, bh - 1); ++ny) {
                for (std::uint32_t nx = bx > 0 ? bx - 1 : 0; nx <= std::min(bx + 1, bw - 1); ++nx) {
                    const Floor& f = floors[static_cast<std::size_t>(ny) * bw + nx];
                    if (!f.known) {
                        continue;
                    }
                    for (std::size_t c = 0; c < 3; ++c) {
                        most[c] = std::max(most[c], f.light[c]);
                        ++(f.light[c] > 0 ? lit[c] : unlit[c]);
                    }
                }
            }
            Rgb& level = levels[static_cast<std::size_t>(by) * bw + bx];
            for (std::size_t c = 0; c < 3; ++c) {
                level[c] = lit[c] >= kWashLitRatio * unlit[c] ? most[c] : 0;
            }
        }
    }
    const auto level = [&](std::uint32_t bx, std::uint32_t by, std::size_t c) -> std::uint32_t {
        return levels[static_cast<std::size_t>(by) * bw + bx][c];
    };
    for (std::uint32_t y = 0; y < height; ++y) {
        const Between v = between(y, bh);
        for (std::uint32_t x = 0; x < width; ++x) {
            const Between u = between(x, bw);
            std::uint8_t* p = &rgba[(static_cast<std::size_t>(y) * width + x) * 4];
            for (std::size_t c = 0; c < 3; ++c) {
                const std::uint32_t sum =
                    level(u.first, v.first, c) * (kWashSpan - u.past) * (kWashSpan - v.past) +
                    level(u.second, v.first, c) * u.past * (kWashSpan - v.past) +
                    level(u.first, v.second, c) * (kWashSpan - u.past) * v.past +
                    level(u.second, v.second, c) * u.past * v.past;
                const auto wash =
                    static_cast<std::uint8_t>((sum + kWashSpan * kWashSpan / 2) / (kWashSpan * kWashSpan));
                p[c] = static_cast<std::uint8_t>(p[c] - std::min(washShare(wash, p[3]), p[c]));
            }
        }
    }
}

} // namespace evr::ui_layer
