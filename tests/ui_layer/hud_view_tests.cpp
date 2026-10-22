#include "ui_layer/hud_view.hpp"
#include "ui_layer/weapon_wheel.hpp"
#include "ui_layer/wheel_bounds.hpp"
#include "wheel_capture_grids.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

using evr::ui_layer::composeHudView;
using evr::ui_layer::describeHudView;
using evr::ui_layer::HeadLockedHud;
using evr::ui_layer::HudView;
using evr::ui_layer::PixelRect;

namespace {

// A GUI image with the drawn samples of a capture's search grid (wheel_capture_grids.hpp) as opaque white
// blocks of the grid's step, the rest transparent.
std::vector<std::uint8_t> imageOf(const wheel_grids::Grid& g) {
    const PixelRect area = evr::ui_layer::wheelSearchRect(g.width, g.height);
    const std::uint32_t step = evr::ui_layer::kWheelGridStep;
    std::vector<std::uint8_t> rgba(std::size_t{g.width} * g.height * 4, 0);
    const std::string runs = g.runs;
    std::uint32_t row = 0;
    std::size_t at = 0;
    while (at <= runs.size()) {
        const std::size_t end = std::min(runs.find('/', at), runs.size());
        const std::string line = runs.substr(at, end - at);
        for (std::size_t p = 0; p < line.size();) {
            const std::size_t comma = std::min(line.find(',', p), line.size());
            const std::string run = line.substr(p, comma - p);
            const std::size_t plus = run.find('+');
            const auto first = static_cast<std::uint32_t>(std::stoul(run.substr(0, plus)));
            const auto count = static_cast<std::uint32_t>(std::stoul(run.substr(plus + 1)));
            for (std::uint32_t y = area.y + row * step; y < area.y + (row + 1) * step && y < g.height; ++y) {
                for (std::uint32_t x = area.x + first * step;
                     x < area.x + (first + count) * step && x < g.width; ++x) {
                    std::fill_n(rgba.begin() + (std::size_t{y} * g.width + x) * 4, 4, std::uint8_t{255});
                }
            }
            p = comma + 1;
        }
        ++row;
        at = end + 1;
    }
    return rgba;
}

struct Box {
    std::int64_t x0 = 1 << 30;
    std::int64_t y0 = 1 << 30;
    std::int64_t x1 = -1;
    std::int64_t y1 = -1;
};

// The opaque pixels of `rgba` (w x h) inside [x0, x1) x [y0, y1).
Box opaque(const std::vector<std::uint8_t>& rgba,
           std::uint32_t w,
           std::int64_t x0,
           std::int64_t y0,
           std::int64_t x1,
           std::int64_t y1) {
    Box b;
    for (std::int64_t y = y0; y < y1; ++y) {
        for (std::int64_t x = x0; x < x1; ++x) {
            if (rgba[(static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) * 4 + 3] > 128) {
                b.x0 = std::min(b.x0, x);
                b.y0 = std::min(b.y0, y);
                b.x1 = std::max(b.x1, x + 1);
                b.y1 = std::max(b.y1, y + 1);
            }
        }
    }
    return b;
}

} // namespace

TEST_CASE("hud view: the centred wheel is drawn straight ahead and its place on the HUD is empty") {
    const wheel_grids::Grid& g = wheel_grids::kGrids[0]; // the larger HUD
    const std::vector<std::uint8_t> rgba = imageOf(g);
    evr::ui_layer::AlphaGrid grid;
    grid.area = evr::ui_layer::wheelSearchRect(g.width, g.height);
    grid.columns = evr::ui_layer::gridColumns(grid.area, grid.step);
    grid.rows = evr::ui_layer::gridRows(grid.area, grid.step);
    for (std::uint32_t r = 0; r < grid.rows; ++r) {
        for (std::uint32_t c = 0; c < grid.columns; ++c) {
            grid.alpha.push_back(
                rgba[(std::size_t{grid.area.y + r * grid.step} * g.width + grid.area.x + c * grid.step) * 4 +
                     3]);
        }
    }
    const std::optional<PixelRect> wheel = evr::ui_layer::measureWheel(grid, g.width, g.height);
    REQUIRE(wheel);
    const PixelRect shown = evr::ui_layer::wideContentRect(g.width, g.height);
    const HeadLockedHud hud = evr::ui_layer::headLockedHud(shown, g.width, g.height, {}, wheel, 1.5f);
    REQUIRE(hud.wheel);
    HudView view;
    view.shown = shown;
    view.panelWidth = 2.0f;
    view.panelCentreY = -0.3f;
    view.pieces = hud.pieces;
    view.wheel = hud.wheel->rect;
    view.wheelWidth = hud.wheel->width;
    view.wheelHeight = hud.wheel->height;
    const evr::ui_layer::HudComposition c = composeHudView(rgba.data(), g.width, g.height, view);
    const std::vector<std::uint8_t>& out = c.image.rgba;
    REQUIRE(out.size() == std::size_t{c.image.width} * c.image.height * 4);
    // The wheel's quad reaches above the UI quad (0.3 m below the eyes): the image grows upwards to hold it.
    CHECK(c.panelX == 0);
    CHECK(c.panelY > 0);
    CHECK(c.image.width == shown.width);
    CHECK(c.image.height == c.panelY + shown.height);
    // The wheel's place on the HUD (right of the middle) holds nothing but the centred quad's overlap.
    const double perMetre = shown.width / 2.0;
    const double quadRight = shown.width / 2.0 + hud.wheel->width * perMetre / 2.0;
    const std::int64_t py = c.panelY;
    const Box left = opaque(out, c.image.width, static_cast<std::int64_t>(std::ceil(quadRight)) + 1,
                            py + g.drawn.y - shown.y, g.drawn.x + g.drawn.width - shown.x,
                            py + g.drawn.y + g.drawn.height - shown.y);
    CHECK(left.x1 < 0);
    // The wheel drawn whole and centred (its quad alone): across the middle, its centre 0.3 m (the UI quad's
    // offset) above the HUD's, as tall as wide (nothing of it cut at the image's edges).
    HudView alone = view;
    alone.pieces.clear();
    const evr::ui_layer::HudComposition a = composeHudView(rgba.data(), g.width, g.height, alone);
    REQUIRE(a.image.width == c.image.width);
    REQUIRE(a.image.height == c.image.height);
    const Box seen = opaque(a.image.rgba, a.image.width, 0, 0, a.image.width, a.image.height);
    REQUIRE(seen.x1 > 0);
    const double cx = (seen.x0 + seen.x1) / 2.0;
    const double cy = (seen.y0 + seen.y1) / 2.0;
    CHECK(std::abs(cx - shown.width / 2.0) < 0.03 * shown.width);
    CHECK(std::abs(cy - (py + shown.height / 2.0 - 0.3 * perMetre)) < 0.03 * shown.height);
    CHECK(seen.y0 > 0);
    CHECK(std::abs((seen.x1 - seen.x0) - (seen.y1 - seen.y0)) < 0.03 * shown.width);
    const std::string text = describeHudView(view);
    // The whole line: the wheel's rectangle and size in full.
    char wheelText[96];
    std::snprintf(wheelText, sizeof(wheelText),
                  "weapon wheel quad straight ahead: GUI rect (%d, %d) %u x %u, ", view.wheel->x,
                  view.wheel->y, view.wheel->width, view.wheel->height);
    CHECK(text.find(wheelText) != std::string::npos);
    CHECK(text.find(" m\n") != std::string::npos);
}

TEST_CASE("hud view: an image or view that does not fit gives nothing; an uncut HUD is the band as it is") {
    std::vector<std::uint8_t> rgba(64 * 48 * 4, 0);
    rgba[(10 * 64 + 10) * 4 + 3] = 255;
    HudView view;
    view.shown = PixelRect{0, 6, 64, 36};
    view.panelWidth = 2.0f;
    view.pieces = {view.shown};
    CHECK(composeHudView(nullptr, 64, 48, view).image.rgba.empty());
    CHECK(composeHudView(rgba.data(), 32, 48, view).image.rgba.empty());
    const evr::ui_layer::HudComposition c = composeHudView(rgba.data(), 64, 48, view);
    CHECK(c.image.width == 64);
    CHECK(c.image.height == 36);
    const std::vector<std::uint8_t>& out = c.image.rgba;
    REQUIRE(out.size() == 64u * 36u * 4u);
    CHECK(out[(4 * 64 + 10) * 4 + 3] == 255);
    CHECK(describeHudView(view).find("no weapon wheel quad") != std::string::npos);
}
