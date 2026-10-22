#include "hud_test_cuts.hpp"
#include "ui_layer/hud_regions.hpp"
#include "ui_layer/weapon_wheel.hpp"
#include "ui_layer/wheel_bounds.hpp"
#include "wheel_capture_grids.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using evr::ui_layer::AlphaGrid;
using evr::ui_layer::BandRect;
using evr::ui_layer::gridColumns;
using evr::ui_layer::gridRows;
using evr::ui_layer::HeadLockedHud;
using evr::ui_layer::headLockedHud;
using evr::ui_layer::kWheelGridStep;
using evr::ui_layer::measureWheel;
using evr::ui_layer::PixelRect;
using evr::ui_layer::toPixels;
using evr::ui_layer::unionRect;
using evr::ui_layer::WheelMeasurement;
using evr::ui_layer::WheelRectTracker;
using evr::ui_layer::wheelSearchRect;
using evr::ui_layer::wideContentRect;

namespace {

std::int64_t right(const PixelRect& r) {
    return static_cast<std::int64_t>(r.x) + r.width;
}

std::int64_t bottom(const PixelRect& r) {
    return static_cast<std::int64_t>(r.y) + r.height;
}

bool contains(const PixelRect& outer, const PixelRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y && right(inner) <= right(outer) &&
           bottom(inner) <= bottom(outer);
}

bool overlap(const PixelRect& a, const PixelRect& b) {
    return a.width != 0 && b.width != 0 && a.x < right(b) && b.x < right(a) && a.y < bottom(b) &&
           b.y < bottom(a);
}

// A capture's grid from its runs (wheel_capture_grids.hpp): drawn samples 255, the rest 0.
AlphaGrid decode(const wheel_grids::Grid& g) {
    AlphaGrid grid;
    grid.area = wheelSearchRect(g.width, g.height);
    grid.step = kWheelGridStep;
    grid.columns = gridColumns(grid.area, grid.step);
    grid.rows = gridRows(grid.area, grid.step);
    grid.alpha.assign(std::size_t{grid.columns} * grid.rows, 0);
    std::uint32_t row = 0;
    const std::string runs = g.runs;
    std::size_t at = 0;
    while (at <= runs.size()) {
        const std::size_t end = std::min(runs.find('/', at), runs.size());
        const std::string line = runs.substr(at, end - at);
        std::size_t p = 0;
        while (p < line.size()) {
            const std::size_t comma = std::min(line.find(',', p), line.size());
            const std::string run = line.substr(p, comma - p);
            const std::size_t plus = run.find('+');
            const auto first = static_cast<std::uint32_t>(std::stoul(run.substr(0, plus)));
            const auto count = static_cast<std::uint32_t>(std::stoul(run.substr(plus + 1)));
            for (std::uint32_t c = first; c < first + count && c < grid.columns && row < grid.rows; ++c) {
                grid.alpha[std::size_t{row} * grid.columns + c] = 255;
            }
            p = comma + 1;
        }
        ++row;
        at = end + 1;
    }
    CHECK(row == grid.rows); // the grid was sampled as wheelSearchRect and kWheelGridStep say
    return grid;
}

// origin/main's fixed wheel rectangle (band x 0.64..0.95, y 0.20..0.75).
PixelRect fixedRect(std::uint32_t width, std::uint32_t height) {
    return toPixels(BandRect{0.64f, 0.20f, 0.95f, 0.75f}, width, height);
}

// The grid sample (column, row) of a pixel, rounded down.
std::int64_t sampleColumn(const AlphaGrid& g, std::int64_t x) {
    return (x - g.area.x) / std::int64_t{g.step};
}

std::int64_t sampleRow(const AlphaGrid& g, std::int64_t y) {
    return (y - g.area.y) / std::int64_t{g.step};
}

void fill(AlphaGrid& g, std::int64_t c0, std::int64_t r0, std::int64_t c1, std::int64_t r1) {
    for (std::int64_t r = std::max<std::int64_t>(r0, 0); r < std::min<std::int64_t>(r1, g.rows); ++r) {
        for (std::int64_t c = std::max<std::int64_t>(c0, 0); c < std::min<std::int64_t>(c1, g.columns); ++c) {
            g.alpha[static_cast<std::size_t>(r * g.columns + c)] = 255;
        }
    }
}

} // namespace

TEST_CASE("wheel bounds: the measured rectangle holds the wheel the game drew, with a small margin") {
    for (const wheel_grids::Grid& g : wheel_grids::kGrids) {
        CAPTURE(g.what);
        const AlphaGrid grid = decode(g);
        const std::optional<PixelRect> wheel = measureWheel(grid, g.width, g.height);
        if (!g.hasWheel) {
            CHECK_FALSE(wheel);
            continue;
        }
        REQUIRE(wheel);
        const PixelRect band = wideContentRect(g.width, g.height);
        CHECK(contains(*wheel, g.drawn));
        CHECK(contains(band, *wheel));
        // Not much more than the wheel: the grid's step and the margin (1% of the band's width) on each side,
        // and a step more for the sampling.
        const std::int64_t most = 2 * std::int64_t{kWheelGridStep} + band.width / 100 + 1;
        CHECK(g.drawn.x - wheel->x <= most);
        CHECK(g.drawn.y - wheel->y <= most);
        CHECK(right(*wheel) - right(g.drawn) <= most);
        CHECK(bottom(*wheel) - bottom(g.drawn) <= most);
        // At least the margin outside the drawn wheel on each side the band allows, less a grid step: `drawn`
        // counts faint edge pixels (alpha 9..15) the grid does not, up to a step past its last drawn sample.
        const std::int64_t margin = band.width / 100 - std::int64_t{kWheelGridStep};
        CHECK(g.drawn.x - wheel->x >= margin);
        CHECK(right(*wheel) - right(g.drawn) >= std::min<std::int64_t>(margin, right(band) - right(g.drawn)));
        CHECK(g.drawn.y - wheel->y >= margin);
        CHECK(bottom(*wheel) - bottom(g.drawn) >= margin);
    }
}

TEST_CASE("wheel bounds: every drawn sample around the wheel the game drew is inside the rectangle") {
    // Within 5% of the wheel's width around it the real captures hold nothing but the wheel (the nearest
    // other HUD element is 5.9% away on the larger HUD): all of it is cut out, whatever shape it is in.
    for (const wheel_grids::Grid& g : wheel_grids::kGrids) {
        if (!g.hasWheel) {
            continue;
        }
        CAPTURE(g.what);
        const AlphaGrid grid = decode(g);
        const std::optional<PixelRect> wheel = measureWheel(grid, g.width, g.height);
        REQUIRE(wheel);
        const std::int64_t near = g.drawn.width / 20;
        std::uint32_t inside = 0;
        std::uint32_t outside = 0;
        for (std::uint32_t r = 0; r < grid.rows; ++r) {
            for (std::uint32_t c = 0; c < grid.columns; ++c) {
                const std::int64_t x = grid.area.x + std::int64_t{c} * grid.step;
                const std::int64_t y = grid.area.y + std::int64_t{r} * grid.step;
                if (grid.alpha[std::size_t{r} * grid.columns + c] < evr::ui_layer::kWheelMinAlpha ||
                    x < g.drawn.x - near || x >= right(g.drawn) + near || y < g.drawn.y - near ||
                    y >= bottom(g.drawn) + near) {
                    continue;
                }
                const bool in = x >= wheel->x && x < right(*wheel) && y >= wheel->y && y < bottom(*wheel);
                (in ? inside : outside) += 1;
            }
        }
        CHECK(inside > 1000);
        CHECK(outside == 0);
    }
}

TEST_CASE(
    "wheel bounds: a part of the wheel apart from its ring is cut out with it; HUD further away is not") {
    const wheel_grids::Grid& g = wheel_grids::kGrids[0]; // the larger HUD
    AlphaGrid grid = decode(g);
    const std::optional<PixelRect> before = measureWheel(grid, g.width, g.height);
    REQUIRE(before);
    // The ring's last drawn sample row.
    std::int64_t last = 0;
    for (std::int64_t r = sampleRow(grid, g.drawn.y); r <= sampleRow(grid, bottom(g.drawn) - 1); ++r) {
        for (std::int64_t c = sampleColumn(grid, g.drawn.x); c <= sampleColumn(grid, right(g.drawn) - 1);
             ++c) {
            if (grid.alpha[static_cast<std::size_t>(r * grid.columns + c)] != 0) {
                last = r;
            }
        }
    }
    // A label 3 empty samples (12 px) under the ring, apart from it: 2.5% of the ring's height is 4 samples.
    const std::int64_t below = last + 4;
    const std::int64_t mid = sampleColumn(grid, g.drawn.x + g.drawn.width / 2);
    fill(grid, mid - 5, below, mid + 5, below + 1);
    std::optional<PixelRect> wheel = measureWheel(grid, g.width, g.height);
    REQUIRE(wheel);
    CHECK(bottom(*wheel) == bottom(*before) + 4 * std::int64_t{kWheelGridStep});
    CHECK(wheel->x == before->x);
    CHECK(wheel->y == before->y);
    // The same 9 empty samples (36 px) under the ring: another HUD element, left where it is.
    grid = decode(g);
    fill(grid, mid - 5, below + 6, mid + 5, below + 7);
    wheel = measureWheel(grid, g.width, g.height);
    REQUIRE(wheel);
    CHECK(bottom(*wheel) == bottom(*before));
}

TEST_CASE("wheel bounds: the crosshair's rings touching the wheel on the left are not the wheel") {
    for (const std::size_t i : {std::size_t{0}, std::size_t{1}}) { // the larger HUD and the rig's default
        const wheel_grids::Grid& g = wheel_grids::kGrids[i];
        CAPTURE(g.what);
        AlphaGrid grid = decode(g);
        const std::optional<PixelRect> alone = measureWheel(grid, g.width, g.height);
        REQUIRE(alone);
        // A ring segment from the band's x 0.50 to inside the wheel's left edge, over the rows of the
        // crosshair's rings (band y 0.43..0.59): one shape with the wheel.
        const PixelRect band = wideContentRect(g.width, g.height);
        const std::int64_t c0 = sampleColumn(grid, band.x + band.width / 2);
        const std::int64_t c1 = sampleColumn(grid, g.drawn.x) + 3;
        const std::int64_t r0 = sampleRow(grid, band.y + band.height * 43 / 100);
        const std::int64_t r1 = sampleRow(grid, band.y + band.height * 59 / 100);
        fill(grid, c0, r0, c1, r1);
        const std::optional<PixelRect> wheel = measureWheel(grid, g.width, g.height);
        REQUIRE(wheel);
        CHECK(contains(*wheel, g.drawn));
        // Cut where the round wheel begins: within a few samples of the rectangle without the rings.
        CHECK(std::abs(wheel->x - alone->x) <= 2 * std::int64_t{kWheelGridStep});
        CHECK(wheel->y == alone->y);
        CHECK(wheel->width <= alone->width + 2 * kWheelGridStep);
        CHECK(wheel->height == alone->height);
    }
}

TEST_CASE("wheel bounds: the larger HUD's wheel fell outside the fixed rectangle, and is cut out whole now") {
    const wheel_grids::Grid& g = wheel_grids::kGrids[0];
    REQUIRE(g.width == 1736);
    // The player's report: the wheel's left part (and its top and bottom) stayed on the head-locked HUD.
    const PixelRect fixed = fixedRect(g.width, g.height);
    CHECK(g.drawn.x < fixed.x);
    CHECK(g.drawn.y < fixed.y);
    CHECK(bottom(g.drawn) > bottom(fixed));
    const std::optional<PixelRect> wheel = measureWheel(decode(g), g.width, g.height);
    REQUIRE(wheel);
    const PixelRect shown = wideContentRect(g.width, g.height);
    const HeadLockedHud hud = headLockedHud(shown, g.width, g.height, {}, wheel, 1.5f);
    REQUIRE(hud.wheel);
    CHECK(contains(hud.wheel->rect, g.drawn));
    for (const PixelRect& p : hud.pieces) {
        CHECK_FALSE(overlap(p, g.drawn)); // no slice of the wheel left beside the centred quad
    }
}

TEST_CASE("wheel bounds: on the default HUD the measured wheel stays clear of the corner blocks") {
    for (std::size_t i = 1; i < std::size(wheel_grids::kGrids); ++i) {
        const wheel_grids::Grid& g = wheel_grids::kGrids[i];
        if (!g.hasWheel) {
            continue;
        }
        CAPTURE(g.what);
        const std::optional<PixelRect> wheel = measureWheel(decode(g), g.width, g.height);
        REQUIRE(wheel);
        for (const PixelRect& cut : hud_test::cutsAt(false, hud_test::corners(), g.width, g.height).cuts) {
            CHECK_FALSE(overlap(*wheel, cut));
        }
    }
}

TEST_CASE("wheel bounds: no wheel in a grid that does not fit the target or holds no wheel's shape") {
    const wheel_grids::Grid& g = wheel_grids::kGrids[1];
    AlphaGrid grid = decode(g);
    REQUIRE(measureWheel(grid, g.width, g.height));
    CHECK_FALSE(measureWheel(grid, 1736, 1896)); // another target's grid
    AlphaGrid shortGrid = grid;
    shortGrid.alpha.pop_back();
    CHECK_FALSE(measureWheel(shortGrid, g.width, g.height));
    AlphaGrid empty = grid;
    empty.alpha.assign(empty.alpha.size(), 0);
    CHECK_FALSE(measureWheel(empty, g.width, g.height));
    // A full-screen backdrop (a menu, a fade): one shape over the whole search rectangle is no wheel.
    AlphaGrid full = grid;
    full.alpha.assign(full.alpha.size(), 255);
    CHECK_FALSE(measureWheel(full, g.width, g.height));
    // Below kWheelMinAlpha nothing counts as drawn.
    AlphaGrid faint = grid;
    for (std::uint8_t& a : faint.alpha) {
        a = a ? evr::ui_layer::kWheelMinAlpha - 1 : 0;
    }
    CHECK_FALSE(measureWheel(faint, g.width, g.height));
}

TEST_CASE("wheel bounds: the tracker merges a time up's measurements and starts the next from them") {
    WheelRectTracker t;
    const PixelRect a{1000, 600, 600, 600};
    const PixelRect b{990, 620, 600, 600};
    // Up, nothing measured yet: the wheel stays where the game draws it.
    CHECK_FALSE(t.update(true, 2048, 2208, WheelMeasurement{}));
    // A measurement made just before the first frame up counts.
    std::optional<PixelRect> r = t.update(true, 2048, 2208, WheelMeasurement{1, a, 2048, 2208});
    REQUIRE(r);
    CHECK(r->x == 1000);
    CHECK(r->width == 600);
    // The next one is merged: the rectangle only grows while the wheel is up.
    r = t.update(true, 2048, 2208, WheelMeasurement{2, b, 2048, 2208});
    REQUIRE(r);
    CHECK(r->x == 990);
    CHECK(r->y == 600);
    CHECK(r->width == 610);
    CHECK(r->height == 620);
    // The same measurement again, or one of another target size: unchanged.
    r = t.update(true, 2048, 2208, WheelMeasurement{2, b, 2048, 2208});
    REQUIRE(r);
    CHECK(r->width == 610);
    r = t.update(true, 2048, 2208, WheelMeasurement{3, PixelRect{0, 0, 10, 10}, 1736, 1896});
    REQUIRE(r);
    CHECK(r->x == 990);
    // Back on the HUD: nothing.
    CHECK_FALSE(t.update(false, 2048, 2208, WheelMeasurement{3, PixelRect{0, 0, 10, 10}, 1736, 1896}));
    // The next time up is centred from its first frame, with the last time up's rectangle.
    r = t.update(true, 2048, 2208, WheelMeasurement{3, PixelRect{0, 0, 10, 10}, 1736, 1896});
    REQUIRE(r);
    CHECK(r->x == 990);
    CHECK(r->width == 610);
    // Its first measurement agrees (every edge within 5% of the width): kept and merged, nothing moves.
    r = t.update(true, 2048, 2208, WheelMeasurement{4, a, 2048, 2208});
    REQUIRE(r);
    CHECK(r->x == 990);
    CHECK(r->y == 600);
    CHECK(r->width == 610);
    CHECK(r->height == 620);
    CHECK_FALSE(t.update(false, 2048, 2208, WheelMeasurement{4, a, 2048, 2208}));
    // The HUD laid out larger meanwhile: the first measurement does not agree and replaces it.
    const PixelRect larger{900, 500, 700, 700};
    r = t.update(true, 2048, 2208, WheelMeasurement{5, larger, 2048, 2208});
    REQUIRE(r);
    CHECK(r->x == 900);
    CHECK(r->y == 500);
    CHECK(r->width == 700);
    CHECK(r->height == 700);
    CHECK_FALSE(t.update(false, 2048, 2208, WheelMeasurement{5, larger, 2048, 2208}));
    // Another target size: the last rectangle is not used.
    CHECK_FALSE(t.update(true, 1736, 1896, WheelMeasurement{5, larger, 2048, 2208}));
}

TEST_CASE("wheel bounds: a target resized while the wheel is up drops the old size's rectangle") {
    WheelRectTracker t;
    const PixelRect a{1000, 600, 600, 600};
    REQUIRE(t.update(true, 2048, 2208, WheelMeasurement{1, a, 2048, 2208}));
    // Resized: the rectangle of 2048x2208 is not shown on 1736x1896, until a measurement of that size.
    CHECK_FALSE(t.update(true, 1736, 1896, WheelMeasurement{1, a, 2048, 2208}));
    CHECK_FALSE(t.update(true, 1736, 1896, WheelMeasurement{2, a, 2048, 2208}));
    const PixelRect b{900, 500, 500, 500};
    std::optional<PixelRect> r = t.update(true, 1736, 1896, WheelMeasurement{3, b, 1736, 1896});
    REQUIRE(r);
    CHECK(r->x == 900);
    CHECK(r->width == 500);
    // Back on the HUD and up again on the new size: the new size's rectangle starts it, not a mix.
    CHECK_FALSE(t.update(false, 1736, 1896, WheelMeasurement{3, b, 1736, 1896}));
    r = t.update(true, 1736, 1896, WheelMeasurement{3, b, 1736, 1896});
    REQUIRE(r);
    CHECK(r->x == 900);
    CHECK(r->width == 500);
    // And on the old size again: nothing kept for it.
    CHECK_FALSE(t.update(false, 1736, 1896, WheelMeasurement{3, b, 1736, 1896}));
    CHECK_FALSE(t.update(true, 2048, 2208, WheelMeasurement{3, b, 1736, 1896}));
}

TEST_CASE("wheel bounds: a wheel still opening does not shrink the last rectangle; a smaller layout does") {
    WheelRectTracker t;
    const PixelRect full{1000, 600, 600, 600};
    REQUIRE(t.update(true, 2048, 2208, WheelMeasurement{1, full, 2048, 2208}));
    REQUIRE_FALSE(t.update(false, 2048, 2208, WheelMeasurement{1, full, 2048, 2208}));
    // Up again, the first readings smaller (inside it), then the whole wheel: the rectangle never shrinks.
    std::uint64_t serial = 1;
    for (const std::uint32_t size : {400u, 500u, 560u, 600u}) {
        const std::int32_t inset = static_cast<std::int32_t>((600 - size) / 2);
        const PixelRect m{1000 + inset, 600 + inset, size, size};
        const std::optional<PixelRect> r =
            t.update(true, 2048, 2208, WheelMeasurement{++serial, m, 2048, 2208});
        REQUIRE(r);
        CHECK(r->x == 1000);
        CHECK(r->width == 600);
    }
    REQUIRE_FALSE(t.update(false, 2048, 2208, WheelMeasurement{serial, full, 2048, 2208}));
    // A smaller HUD layout: smaller readings for kWheelShrinkReadings readings replace it.
    const PixelRect small{1050, 650, 500, 500};
    std::optional<PixelRect> r;
    for (std::uint32_t i = 1; i < evr::ui_layer::kWheelShrinkReadings; ++i) {
        r = t.update(true, 2048, 2208, WheelMeasurement{++serial, small, 2048, 2208});
        REQUIRE(r);
        CHECK(r->width == 600);
    }
    r = t.update(true, 2048, 2208, WheelMeasurement{++serial, small, 2048, 2208});
    REQUIRE(r);
    CHECK(r->x == 1050);
    CHECK(r->width == 500);
}

TEST_CASE(
    "wheel bounds: read every frame until the rectangle is stable, then every 8th; a change reads again") {
    using evr::ui_layer::kWheelSlowEvery;
    using evr::ui_layer::kWheelStableReadings;
    using evr::ui_layer::WheelReadCadence;
    WheelReadCadence c;
    CHECK_FALSE(c.next(false));
    const PixelRect a{1000, 600, 600, 600};
    const PixelRect b{990, 600, 610, 600};
    // Every frame while the readings have not yet found the same rectangle kWheelStableReadings times in a
    // row.
    for (std::uint32_t i = 0; i < kWheelStableReadings; ++i) {
        CHECK(c.next(true));
        c.measured(a);
    }
    // Stable: every kWheelSlowEvery-th frame.
    std::uint32_t reads = 0;
    for (std::uint32_t i = 1; i <= 10 * kWheelSlowEvery; ++i) {
        const bool read = c.next(true);
        CHECK(read == (i % kWheelSlowEvery == 0));
        if (read) {
            ++reads;
            c.measured(a);
        }
    }
    CHECK(reads == 10);
    // Another rectangle (a layout change, a bar the margin does not hold): every frame again until stable.
    c.measured(b);
    CHECK(c.next(true));
    c.measured(b);
    CHECK(c.next(true));
    c.measured(b);
    CHECK_FALSE(c.next(true));
    // No wheel found: every frame.
    c.measured(std::nullopt);
    CHECK(c.next(true));
    CHECK(c.next(true));
    // Down, then up again: the first frame is read, and every frame until stable.
    CHECK_FALSE(c.next(false));
    c.measured(a); // a reading of the time up that is over: ignored
    for (std::uint32_t i = 0; i < kWheelStableReadings; ++i) {
        CHECK(c.next(true));
        c.measured(b);
    }
    CHECK_FALSE(c.next(true));
}

TEST_CASE("wheel bounds: rectangles are close when every edge is within 5% of the measured width") {
    using evr::ui_layer::closeRects;
    const PixelRect m{1000, 600, 600, 600};
    CHECK(closeRects(m, m));
    CHECK(closeRects(PixelRect{970, 630, 660, 540}, m)); // 30 px = 5% of 600 on every edge
    CHECK_FALSE(closeRects(PixelRect{969, 600, 631, 600}, m));
    CHECK_FALSE(closeRects(PixelRect{1000, 600, 631, 600}, m));
    CHECK_FALSE(closeRects(PixelRect{1000, 569, 600, 631}, m));
    CHECK_FALSE(closeRects(PixelRect{1000, 600, 600, 631}, m));
}

TEST_CASE("wheel bounds: the union of two rectangles ignores an empty one") {
    const PixelRect a{10, 20, 30, 40};
    const PixelRect u = unionRect(a, PixelRect{});
    CHECK(u.x == 10);
    CHECK(u.width == 30);
    const PixelRect v = unionRect(PixelRect{}, a);
    CHECK(v.y == 20);
    CHECK(v.height == 40);
    const PixelRect w = unionRect(a, PixelRect{0, 50, 5, 20});
    CHECK(w.x == 0);
    CHECK(w.y == 20);
    CHECK(w.width == 40);
    CHECK(w.height == 50);
}
