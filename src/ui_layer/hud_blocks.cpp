#include "ui_layer/hud_blocks.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace evr::ui_layer {

namespace {

using Box = WheelScratch::Box;

std::int64_t right(const PixelRect& r) {
    return std::int64_t{r.x} + r.width;
}

std::int64_t bottom(const PixelRect& r) {
    return std::int64_t{r.y} + r.height;
}

// Whether `b` touches the search rectangle's top row or its inner column (the side toward the band's middle).
bool touchesInner(const Box& b, WristBlock block, const AlphaGrid& g) {
    return b.r0 == 0 || (block == WristBlock::Vitals ? b.c1 + 1 == g.columns : b.c0 == 0);
}

// The gap in samples between two boxes along each axis (0 when they overlap on it).
std::uint32_t gap(std::uint32_t a0, std::uint32_t a1, std::uint32_t b0, std::uint32_t b1) {
    if (b0 > a1) {
        return b0 - a1 - 1;
    }
    if (a0 > b1) {
        return a0 - b1 - 1;
    }
    return 0;
}

bool sameRect(const PixelRect& a, const PixelRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

} // namespace

PixelRect cornerSearchRect(WristBlock block, std::uint32_t width, std::uint32_t height) {
    const PixelRect band = wideContentRect(width, height);
    if (block == WristBlock::Abilities || band.width == 0 || band.height == 0) {
        return {};
    }
    const auto bw = static_cast<double>(band.width);
    const auto top =
        static_cast<std::int64_t>(std::floor(static_cast<double>(band.height) * kCornerSearchTop));
    std::int64_t x0 = band.x;
    std::int64_t x1 = right(band);
    if (block == WristBlock::Vitals) {
        x1 = band.x + std::min<std::int64_t>(static_cast<std::int64_t>(std::ceil(bw * kCornerSearchWidth)),
                                             band.width);
    } else {
        x0 = band.x + static_cast<std::int64_t>(std::floor(bw * (1.0 - kCornerSearchWidth)));
    }
    return PixelRect{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(band.y + top),
                     static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(band.height - top)};
}

std::optional<PixelRect> measureCornerBlock(const AlphaGrid& grid,
                                            WristBlock block,
                                            std::uint32_t width,
                                            std::uint32_t height,
                                            WheelScratch* scratch) {
    const PixelRect band = wideContentRect(width, height);
    const PixelRect area = cornerSearchRect(block, width, height);
    if (area.width == 0 || !sameRect(grid.area, area) || grid.step == 0 || grid.columns == 0 ||
        grid.rows == 0 || grid.alpha.size() != std::size_t{grid.columns} * grid.rows ||
        grid.columns != gridColumns(grid.area, grid.step) || grid.rows != gridRows(grid.area, grid.step)) {
        return std::nullopt;
    }
    // A layer over most of the corner (a fade, a menu's backdrop) holds no block: not labelled (on the rig
    // the readings of the map start's fade took up to 1.1 ms at 2048x2208, labelling it whole).
    const auto drawn = static_cast<std::size_t>(std::count_if(
        grid.alpha.begin(), grid.alpha.end(), [](std::uint8_t a) { return a >= kWheelMinAlpha; }));
    if (static_cast<double>(drawn) > kCornerMaxDrawn * static_cast<double>(grid.alpha.size())) {
        return std::nullopt;
    }
    WheelScratch local;
    WheelScratch& s = scratch ? *scratch : local;
    s.label.assign(grid.alpha.size(), 0);
    s.boxes.assign(1, Box{}); // label 0: not labelled
    for (std::uint32_t r = 0; r < grid.rows; ++r) {
        for (std::uint32_t c = 0; c < grid.columns; ++c) {
            const std::size_t at = std::size_t{r} * grid.columns + c;
            if (s.label[at] == 0 && grid.alpha[at] >= kWheelMinAlpha) {
                s.boxes.push_back(labelShape(grid, c, r, static_cast<std::uint32_t>(s.boxes.size()), s));
            }
        }
    }
    // The seed: the largest shape clear of the inner edges.
    std::size_t seed = 0;
    for (std::size_t i = 1; i < s.boxes.size(); ++i) {
        if (!touchesInner(s.boxes[i], block, grid) &&
            (seed == 0 || s.boxes[i].samples > s.boxes[seed].samples)) {
            seed = i;
        }
    }
    if (seed == 0) {
        return std::nullopt;
    }
    const auto bw = static_cast<double>(band.width);
    const auto bh = static_cast<double>(band.height);
    const auto bandY = [&](std::uint32_t row) {
        return (static_cast<double>(grid.area.y) + static_cast<double>(row) * grid.step - band.y) / bh;
    };
    const Box& sb = s.boxes[seed];
    if (bandY(sb.r1 + 1) < kCornerSeedLow ||
        static_cast<double>(sb.c1 - sb.c0 + 1) * grid.step < kCornerMinSeed * bw) {
        return std::nullopt;
    }
    // Every shape near the block joins it, until none comes within the reach.
    const auto reach = static_cast<std::uint32_t>(std::ceil(kCornerGather * bh / grid.step));
    Box b = sb;
    std::vector<std::uint8_t>& joined = s.joined;
    joined.assign(s.boxes.size(), 0);
    joined[seed] = 1;
    for (bool grew = true; grew;) {
        grew = false;
        for (std::size_t i = 1; i < s.boxes.size(); ++i) {
            const Box& p = s.boxes[i];
            if (joined[i] || touchesInner(p, block, grid) || gap(b.c0, b.c1, p.c0, p.c1) > reach ||
                gap(b.r0, b.r1, p.r0, p.r1) > reach) {
                continue;
            }
            joined[i] = 1;
            grew = true;
            b.c0 = std::min(b.c0, p.c0);
            b.c1 = std::max(b.c1, p.c1);
            b.r0 = std::min(b.r0, p.r0);
            b.r1 = std::max(b.r1, p.r1);
        }
    }
    if (static_cast<double>(b.c1 - b.c0 + 1) * grid.step > kCornerMaxSize * bw ||
        static_cast<double>(b.r1 - b.r0 + 1) * grid.step > kCornerMaxSize * bh) {
        return std::nullopt;
    }
    // A drawn pixel can lie up to a step short of the next sample; then the margin.
    const std::int64_t grow = std::int64_t{grid.step} + std::max<std::int64_t>(1, band.width / 100);
    const std::int64_t x0 =
        std::max<std::int64_t>(grid.area.x + std::int64_t{b.c0} * grid.step - grow, band.x);
    const std::int64_t y0 =
        std::max<std::int64_t>(grid.area.y + std::int64_t{b.r0} * grid.step - grow, band.y);
    const std::int64_t x1 =
        std::min<std::int64_t>(grid.area.x + std::int64_t{b.c1} * grid.step + grow, right(band));
    const std::int64_t y1 =
        std::min<std::int64_t>(grid.area.y + std::int64_t{b.r1} * grid.step + grow, bottom(band));
    if (x1 <= x0 || y1 <= y0) {
        return std::nullopt;
    }
    return PixelRect{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0),
                     static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)};
}

void CornerRectTracker::Kept::add(const PixelRect& r) {
    rects[next] = r;
    next = (next + 1) % rects.size();
    count = std::min(count + 1, rects.size());
}

std::optional<PixelRect> CornerRectTracker::Kept::merged() const {
    if (count == 0) {
        return std::nullopt;
    }
    PixelRect out{};
    for (std::size_t i = 0; i < count; ++i) {
        out = unionRect(out, rects[i]);
    }
    return out;
}

CornerBlocks CornerRectTracker::update(std::uint32_t width,
                                       std::uint32_t height,
                                       const CornerMeasurement& latest,
                                       bool passOver) {
    if (width != width_ || height != height_) {
        // A rectangle of another target size does not fit this one.
        width_ = width;
        height_ = height;
        vitals_ = {};
        weapon_ = {};
    }
    if (latest.serial > seen_) {
        seen_ = latest.serial;
        if (!passOver && latest.width == width && latest.height == height) {
            if (latest.vitals && latest.vitals->width != 0 && latest.vitals->height != 0) {
                vitals_.add(*latest.vitals);
            }
            if (latest.weapon && latest.weapon->width != 0 && latest.weapon->height != 0) {
                weapon_.add(*latest.weapon);
            }
        }
    }
    return CornerBlocks{vitals_.merged(), weapon_.merged()};
}

bool CornerReadCadence::due(bool wanted, std::uint32_t width, std::uint32_t height) {
    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        found_ = false;
        fastLeft_ = kCornerFastReadings;
        since_ = 0;
    }
    if (!wanted) {
        return false;
    }
    if (!found_ && fastLeft_ > 0) {
        return true;
    }
    if (since_ < kCornerSlowEvery) {
        ++since_;
    }
    return since_ >= kCornerSlowEvery;
}

void CornerReadCadence::consume() {
    if (!found_ && fastLeft_ > 0) {
        --fastLeft_;
    } else {
        since_ = 0;
    }
}

void CornerReadCadence::measured(std::uint32_t width, std::uint32_t height, bool both) {
    if (both && width == width_ && height == height_) {
        found_ = true;
    }
}

} // namespace evr::ui_layer
