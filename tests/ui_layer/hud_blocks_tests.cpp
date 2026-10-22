#include "corner_capture_grids.hpp"
#include "ui_layer/hud_blocks.hpp"
#include "ui_layer/hud_regions.hpp"
#include "ui_layer/wheel_bounds.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using evr::ui_layer::AlphaGrid;
using evr::ui_layer::BandRect;
using evr::ui_layer::CornerBlocks;
using evr::ui_layer::CornerMeasurement;
using evr::ui_layer::CornerReadCadence;
using evr::ui_layer::CornerRectTracker;
using evr::ui_layer::cornerSearchRect;
using evr::ui_layer::cutRect;
using evr::ui_layer::gridColumns;
using evr::ui_layer::gridRows;
using evr::ui_layer::kWheelGridStep;
using evr::ui_layer::measureCornerBlock;
using evr::ui_layer::PixelRect;
using evr::ui_layer::toPixels;
using evr::ui_layer::wideContentRect;
using evr::ui_layer::WristBlock;

namespace {

std::int64_t right(const PixelRect& r) {
    return std::int64_t{r.x} + r.width;
}

std::int64_t bottom(const PixelRect& r) {
    return std::int64_t{r.y} + r.height;
}

bool contains(const PixelRect& outer, const PixelRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y && right(inner) <= right(outer) &&
           bottom(inner) <= bottom(outer);
}

bool same(const PixelRect& a, const PixelRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

// A corner's grid of a capture from its runs (corner_capture_grids.hpp): drawn samples 255, the rest 0.
AlphaGrid decode(const corner_grids::Grid& g, WristBlock block) {
    AlphaGrid grid;
    grid.area = cornerSearchRect(block, g.width, g.height);
    grid.step = kWheelGridStep;
    grid.columns = gridColumns(grid.area, grid.step);
    grid.rows = gridRows(grid.area, grid.step);
    grid.alpha.assign(std::size_t{grid.columns} * grid.rows, 0);
    const std::string runs = block == WristBlock::Vitals ? g.vitalsRuns : g.weaponRuns;
    std::uint32_t row = 0;
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
    REQUIRE(row == grid.rows);
    return grid;
}

// Draws a filled rectangle (target pixels) into a corner's grid, on the samples inside it.
void draw(AlphaGrid& grid, const PixelRect& r) {
    for (std::uint32_t row = 0; row < grid.rows; ++row) {
        for (std::uint32_t c = 0; c < grid.columns; ++c) {
            const std::int64_t x = grid.area.x + std::int64_t{c} * grid.step;
            const std::int64_t y = grid.area.y + std::int64_t{row} * grid.step;
            if (x >= r.x && x < right(r) && y >= r.y && y < bottom(r)) {
                grid.alpha[std::size_t{row} * grid.columns + c] = 255;
            }
        }
    }
}

const corner_grids::Grid& gridNamed(const char* start) {
    for (const corner_grids::Grid& g : corner_grids::kGrids) {
        if (std::string(g.what).rfind(start, 0) == 0) {
            return g;
        }
    }
    FAIL("no grid " << start);
    return corner_grids::kGrids[0];
}

} // namespace

TEST_CASE("hud blocks: each corner's search rectangle is the band's bottom 40% at its side") {
    const std::uint32_t sizes[][2] = {{2048, 2208}, {1280, 720}, {1280, 1400}, {2048, 864}, {1736, 1896}};
    for (const auto& size : sizes) {
        const std::uint32_t w = size[0];
        const std::uint32_t h = size[1];
        CAPTURE(w);
        CAPTURE(h);
        const PixelRect band = wideContentRect(w, h);
        const PixelRect v = cornerSearchRect(WristBlock::Vitals, w, h);
        const PixelRect r = cornerSearchRect(WristBlock::Weapon, w, h);
        CHECK(contains(band, v));
        CHECK(contains(band, r));
        CHECK(v.x == band.x);
        CHECK(right(r) == right(band));
        CHECK(bottom(v) == bottom(band));
        CHECK(bottom(r) == bottom(band));
        CHECK(v.y == r.y);
        CHECK(static_cast<double>(v.y - band.y) / band.height == doctest::Approx(0.6).epsilon(0.01));
        CHECK(static_cast<double>(v.width) / band.width == doctest::Approx(0.4).epsilon(0.01));
        CHECK(static_cast<double>(r.width) / band.width == doctest::Approx(0.4).epsilon(0.01));
        CHECK(cornerSearchRect(WristBlock::Abilities, w, h).width == 0);
    }
    CHECK(cornerSearchRect(WristBlock::Vitals, 0, 0).width == 0);
}

TEST_CASE("hud blocks: each capture's blocks are measured where the game drew them, at every HUD Scale") {
    for (const corner_grids::Grid& g : corner_grids::kGrids) {
        CAPTURE(g.what);
        const PixelRect band = wideContentRect(g.width, g.height);
        // The grid's step (a drawn pixel between samples) and 1% of the band's width, then one more sample.
        const std::int64_t slack = 2 * std::int64_t{kWheelGridStep} + band.width / 100;
        for (const WristBlock block : {WristBlock::Vitals, WristBlock::Weapon}) {
            CAPTURE(block == WristBlock::Vitals ? "vitals" : "weapon");
            const bool has = block == WristBlock::Vitals ? g.hasVitals : g.hasWeapon;
            const PixelRect& drawn = block == WristBlock::Vitals ? g.drawnVitals : g.drawnWeapon;
            const std::optional<PixelRect> m = measureCornerBlock(decode(g, block), block, g.width, g.height);
            REQUIRE(m.has_value() == has);
            if (!m) {
                continue;
            }
            // All of the block, inside the band ...
            CHECK(contains(*m, drawn));
            CHECK(contains(band, *m));
            // ... and no more than the margin around it.
            CHECK(drawn.x - m->x <= slack);
            CHECK(drawn.y - m->y <= slack);
            CHECK(right(*m) - right(drawn) <= slack);
            CHECK(bottom(*m) - bottom(drawn) <= slack);
        }
    }
}

TEST_CASE(
    "hud blocks: the Large blocks spill past the fixed rectangles made for Medium, not past their cuts") {
    // Where the fixed cuts were (origin/main: vitals x 0..0.215, y 0.76..1; ammo x 0.765..1, y 0.76..1 of the
    // band): the Large block's edge stays on the head-locked HUD.
    const corner_grids::Grid& g = gridNamed("the rig, 2048x2208, the game's Large");
    const PixelRect fixedVitals = toPixels(BandRect{0.0f, 0.76f, 0.215f, 1.0f}, g.width, g.height);
    const PixelRect fixedWeapon = toPixels(BandRect{0.765f, 0.76f, 1.0f, 1.0f}, g.width, g.height);
    CHECK_FALSE(contains(fixedVitals, g.drawnVitals));
    CHECK_FALSE(contains(fixedWeapon, g.drawnWeapon));
    // The cuts from the measured blocks hold them whole.
    const auto v = measureCornerBlock(decode(g, WristBlock::Vitals), WristBlock::Vitals, g.width, g.height);
    const auto w = measureCornerBlock(decode(g, WristBlock::Weapon), WristBlock::Weapon, g.width, g.height);
    REQUIRE(v);
    REQUIRE(w);
    CHECK(contains(cutRect(WristBlock::Vitals, *v, g.width, g.height), g.drawnVitals));
    CHECK(contains(cutRect(WristBlock::Weapon, *w, g.width, g.height), g.drawnWeapon));
}

TEST_CASE(
    "hud blocks: a shape near the block joins it, one further off or reaching in from the middle does not") {
    const corner_grids::Grid& g = gridNamed("the rig, 2048x2208, the game's Medium");
    const PixelRect band = wideContentRect(g.width, g.height);
    AlphaGrid grid = decode(g, WristBlock::Vitals);
    const auto before = measureCornerBlock(grid, WristBlock::Vitals, g.width, g.height);
    REQUIRE(before);
    // A subtitle line at the bottom middle reaching into the corner from the band's middle (it touches the
    // search rectangle's inner edge), level with the health bar.
    AlphaGrid subtitle = grid;
    draw(subtitle, PixelRect{band.x + static_cast<std::int32_t>(band.width * 28 / 100), g.drawnVitals.y + 120,
                             band.width * 20 / 100, 24});
    const auto withSubtitle = measureCornerBlock(subtitle, WristBlock::Vitals, g.width, g.height);
    REQUIRE(withSubtitle);
    CHECK(same(*withSubtitle, *before));
    // A pickup notice clear of the edges but 4% of the band's height above the block: not the block's.
    AlphaGrid far = grid;
    const auto above = static_cast<std::int32_t>(band.height * 4 / 100);
    draw(far, PixelRect{g.drawnVitals.x + 40, g.drawnVitals.y - above - 20, 120, 20});
    const auto withFar = measureCornerBlock(far, WristBlock::Vitals, g.width, g.height);
    REQUIRE(withFar);
    CHECK(same(*withFar, *before));
    // A key hint label 1% of the band's height above it: the block's own, so the block grows to hold it.
    AlphaGrid near = grid;
    const auto close = static_cast<std::int32_t>(band.height / 100);
    const PixelRect label{g.drawnVitals.x + 40, g.drawnVitals.y - close - 20, 120, 20};
    draw(near, label);
    const auto withNear = measureCornerBlock(near, WristBlock::Vitals, g.width, g.height);
    REQUIRE(withNear);
    CHECK(contains(*withNear, label));
    CHECK(contains(*withNear, g.drawnVitals));
}

TEST_CASE("hud blocks: a layer over the corner, a block too high or too small, or another target's grid give "
          "nothing") {
    const corner_grids::Grid& g = gridNamed("the rig, 2048x2208, the game's Medium");
    const PixelRect band = wideContentRect(g.width, g.height);
    AlphaGrid grid = decode(g, WristBlock::Vitals);
    // Filling the corner (a pause menu's backdrop, a fade): all of it reaches the edges.
    AlphaGrid filled = grid;
    draw(filled, grid.area);
    CHECK_FALSE(measureCornerBlock(filled, WristBlock::Vitals, g.width, g.height).has_value());
    // A box clear of the edges but filling most of the corner is no block, and neither is its outline (a
    // frame around most of the corner: few samples, too large).
    AlphaGrid box = grid;
    const PixelRect most{grid.area.x + 8, grid.area.y + 8, grid.area.width - 40, grid.area.height - 8};
    draw(box, most);
    CHECK_FALSE(measureCornerBlock(box, WristBlock::Vitals, g.width, g.height).has_value());
    AlphaGrid outline = grid;
    draw(outline, PixelRect{most.x, most.y, most.width, 8});
    draw(outline, PixelRect{most.x, most.y, 8, most.height});
    draw(outline, PixelRect{most.x + static_cast<std::int32_t>(most.width) - 8, most.y, 8, most.height});
    CHECK_FALSE(measureCornerBlock(outline, WristBlock::Vitals, g.width, g.height).has_value());
    // Only a shape high in the corner (the block hidden, a notice left): no seed low in the band.
    AlphaGrid high(grid);
    std::fill(high.alpha.begin(), high.alpha.end(), 0);
    draw(high, PixelRect{band.x + 100, band.y + static_cast<std::int32_t>(band.height * 70 / 100), 300, 40});
    CHECK_FALSE(measureCornerBlock(high, WristBlock::Vitals, g.width, g.height).has_value());
    // Only a small mark low in the corner: too narrow for a block's bar.
    AlphaGrid small(grid);
    std::fill(small.alpha.begin(), small.alpha.end(), 0);
    draw(small, PixelRect{band.x + 100, band.y + static_cast<std::int32_t>(band.height * 92 / 100), 40, 24});
    CHECK_FALSE(measureCornerBlock(small, WristBlock::Vitals, g.width, g.height).has_value());
    // Another target size, or the other corner's grid.
    CHECK_FALSE(measureCornerBlock(grid, WristBlock::Vitals, 2064, 2100).has_value());
    CHECK_FALSE(measureCornerBlock(grid, WristBlock::Weapon, g.width, g.height).has_value());
    CHECK_FALSE(measureCornerBlock(AlphaGrid{}, WristBlock::Vitals, g.width, g.height).has_value());
}

TEST_CASE("hud blocks: the tracker keeps the union of the last readings, per target size") {
    CornerRectTracker t;
    CornerMeasurement m;
    m.width = 2048;
    m.height = 2208;
    CHECK_FALSE(t.update(2048, 2208, m).vitals.has_value()); // nothing measured yet
    const PixelRect small{60, 1500, 320, 120};
    const PixelRect lives{60, 1420, 320, 200}; // the extra lives row above it
    m.serial = 1;
    m.vitals = small;
    CornerBlocks b = t.update(2048, 2208, m);
    REQUIRE(b.vitals);
    CHECK(same(*b.vitals, small));
    CHECK_FALSE(b.weapon.has_value());
    // A block that grows is cut whole from its next reading on.
    m.serial = 2;
    m.vitals = lives;
    b = t.update(2048, 2208, m);
    CHECK(same(*b.vitals, lives));
    // The same reading seen again changes nothing; a reading without the block does not count.
    b = t.update(2048, 2208, m);
    CHECK(same(*b.vitals, lives));
    m.serial = 3;
    m.vitals.reset();
    b = t.update(2048, 2208, m);
    CHECK(same(*b.vitals, lives));
    // It shrinks back once the larger reading is kCornerKeptReadings readings old.
    m.vitals = small;
    for (std::size_t i = 0; i + 1 < evr::ui_layer::kCornerKeptReadings; ++i) {
        ++m.serial;
        b = t.update(2048, 2208, m);
        CHECK(same(*b.vitals, lives));
    }
    ++m.serial;
    b = t.update(2048, 2208, m);
    CHECK(same(*b.vitals, small));
    // A reading of another target size is not used; a new size drops what was measured.
    ++m.serial;
    m.width = 1280;
    m.height = 720;
    m.vitals = PixelRect{30, 600, 200, 80};
    b = t.update(2048, 2208, m);
    CHECK(same(*b.vitals, small));
    b = t.update(1280, 720, m);
    CHECK_FALSE(b.vitals.has_value()); // that reading was seen already (for the old size)
    ++m.serial;
    b = t.update(1280, 720, m);
    REQUIRE(b.vitals);
    CHECK(same(*b.vitals, PixelRect{30, 600, 200, 80}));
}

namespace {

// One GUI copy through the cadence as the readback runs it: due, then consumed when a slot was free.
bool readCopy(CornerReadCadence& c, bool wanted, std::uint32_t w, std::uint32_t h, bool slotFree = true) {
    if (!c.due(wanted, w, h) || !slotFree) {
        return false;
    }
    c.consume();
    return true;
}

} // namespace

TEST_CASE("hud blocks: read every wanted copy until both blocks are found, then every 30th") {
    CornerReadCadence c;
    CHECK_FALSE(readCopy(c, false, 2048, 2208)); // not wanted: no read
    CHECK(readCopy(c, true, 2048, 2208));
    CHECK(readCopy(c, true, 2048, 2208));
    c.measured(2048, 2208, false); // a reading without both blocks: keep reading every copy
    CHECK(readCopy(c, true, 2048, 2208));
    c.measured(2048, 2208, true);
    int reads = 0;
    for (int i = 0; i < 300; ++i) {
        reads += readCopy(c, true, 2048, 2208) ? 1 : 0;
    }
    CHECK(reads == 300 / static_cast<int>(evr::ui_layer::kCornerSlowEvery));
    // Not wanted copies do not count toward the next read.
    for (int i = 0; i < 100; ++i) {
        CHECK_FALSE(readCopy(c, false, 2048, 2208));
    }
    // A reading of another size does not count; a new target size reads every copy again.
    c.measured(1280, 720, true);
    CHECK(readCopy(c, true, 1280, 720));
    CHECK(readCopy(c, true, 1280, 720));
    c.measured(1280, 720, true);
    CHECK_FALSE(readCopy(c, true, 1280, 720));
    // A HUD with no blocks to find (hidden by the player): every copy for kCornerFastReadings reads, then
    // slow.
    CornerReadCadence hidden;
    int fast = 0;
    while (readCopy(hidden, true, 2048, 2208)) {
        hidden.measured(2048, 2208, false);
        ++fast;
        REQUIRE(fast <= 100);
    }
    CHECK(fast == static_cast<int>(evr::ui_layer::kCornerFastReadings));
    reads = 0;
    for (int i = 0; i < 300; ++i) {
        reads += readCopy(hidden, true, 2048, 2208) ? 1 : 0;
    }
    CHECK(reads == 300 / static_cast<int>(evr::ui_layer::kCornerSlowEvery));
}

TEST_CASE("hud blocks: a due read that finds no free slot stays due, fast or slow") {
    CornerReadCadence c;
    // Fast: a copy with no slot does not use up one of the fast reads.
    for (int i = 0; i < 100; ++i) {
        CHECK_FALSE(readCopy(c, true, 2048, 2208, false));
    }
    int fast = 0;
    while (readCopy(c, true, 2048, 2208)) {
        c.measured(2048, 2208, false);
        ++fast;
        REQUIRE(fast <= 100);
    }
    CHECK(fast == static_cast<int>(evr::ui_layer::kCornerFastReadings));
    // Slow: the 30th copy finds no slot; the 31st reads, not the 60th.
    CornerReadCadence s;
    CHECK(readCopy(s, true, 2048, 2208));
    s.measured(2048, 2208, true);
    for (std::uint32_t i = 1; i < evr::ui_layer::kCornerSlowEvery; ++i) {
        CHECK_FALSE(readCopy(s, true, 2048, 2208));
    }
    CHECK_FALSE(readCopy(s, true, 2048, 2208, false));
    CHECK(readCopy(s, true, 2048, 2208));
}
