#include "ui_layer/backdrop.hpp"
#include "ui_layer/gun_crosshair.hpp"
#include "ui_layer/perf_metrics.hpp"
#include "ui_layer/ui_settings.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

using evr::ui_layer::gunCrosshairCuts;
using evr::ui_layer::gunCrosshairRect;
using evr::ui_layer::gunCrosshairSize;
using evr::ui_layer::kGunCrosshairHeight;
using evr::ui_layer::kGunCrosshairWidth;
using evr::ui_layer::PixelRect;

namespace {

std::uint64_t area(const PixelRect& r) {
    return static_cast<std::uint64_t>(r.width) * r.height;
}

std::uint64_t area(const std::vector<PixelRect>& rects) {
    std::uint64_t sum = 0;
    for (const PixelRect& r : rects) {
        sum += area(r);
    }
    return sum;
}

bool inside(const PixelRect& r, const PixelRect& in) {
    return r.x >= in.x && r.y >= in.y && r.x + static_cast<std::int64_t>(r.width) <= in.x + in.width &&
           r.y + static_cast<std::int64_t>(r.height) <= in.y + in.height;
}

bool overlaps(const PixelRect& a, const PixelRect& b) {
    return a.x < b.x + static_cast<std::int64_t>(b.width) && b.x < a.x + static_cast<std::int64_t>(a.width) &&
           a.y < b.y + static_cast<std::int64_t>(b.height) && b.y < a.y + static_cast<std::int64_t>(a.height);
}

// The centre of `r` is the centre of its band, to half a pixel.
void checkCentred(const PixelRect& r, const PixelRect& band) {
    CHECK(std::abs((r.x + r.width / 2.0) - (band.x + band.width / 2.0)) <= 0.5);
    CHECK(std::abs((r.y + r.height / 2.0) - (band.y + band.height / 2.0)) <= 0.5);
}

} // namespace

TEST_CASE("gun crosshair: the rectangle is centred in the band, sized by the band's height") {
    // The simulator's near-square eye target: a 1280 x 720 band 340 rows down.
    const PixelRect r = gunCrosshairRect(1280, 1400);
    const PixelRect band = evr::ui_layer::wideContentRect(1280, 1400);
    CHECK(r.width == static_cast<std::uint32_t>(std::lround(kGunCrosshairWidth * 720.0f)));
    CHECK(r.height == static_cast<std::uint32_t>(std::lround(kGunCrosshairHeight * 720.0f)));
    CHECK(inside(r, band));
    checkCentred(r, band);
    // A 16:9 target is its own band; a wider one has a band of columns: the same fractions of its height.
    const PixelRect flat = gunCrosshairRect(1920, 1080);
    CHECK(flat.height == static_cast<std::uint32_t>(std::lround(kGunCrosshairHeight * 1080.0f)));
    checkCentred(flat, {0, 0, 1920, 1080});
    const PixelRect wide = gunCrosshairRect(2048, 864);
    const PixelRect wideBand = evr::ui_layer::wideContentRect(2048, 864);
    CHECK(wide.height == static_cast<std::uint32_t>(std::lround(kGunCrosshairHeight * 864.0f)));
    CHECK(inside(wide, wideBand));
    checkCentred(wide, wideBand);
    // The same share of the band whatever the target's size: the reticle scales with the band.
    const PixelRect big = gunCrosshairRect(2064, 2100);
    const PixelRect bigBand = evr::ui_layer::wideContentRect(2064, 2100);
    CHECK(std::abs(static_cast<double>(big.height) / bigBand.height - static_cast<double>(r.height) / 720.0) <
          0.002);
    // Nothing to move in an empty target.
    CHECK(area(gunCrosshairRect(0, 0)) == 0);
    CHECK(area(gunCrosshairRect(1280, 0)) == 0);
}

TEST_CASE("gun crosshair: without markers the whole rectangle moves and is cleared from the HUD") {
    const PixelRect r = gunCrosshairRect(1280, 1400);
    const evr::ui_layer::GunCrosshairCuts cuts = gunCrosshairCuts(r, {});
    REQUIRE(cuts.hud.size() == 1);
    CHECK(cuts.hud[0].x == r.x);
    CHECK(cuts.hud[0].y == r.y);
    CHECK(area(cuts.hud) == area(r));
    CHECK(cuts.moved.empty());
    // A marker box away from the middle changes nothing.
    const evr::ui_layer::GunCrosshairCuts away = gunCrosshairCuts(r, {{10, 400, 64, 70}});
    CHECK(area(away.hud) == area(r));
    CHECK(away.moved.empty());
    // An empty rectangle: nothing at all.
    const evr::ui_layer::GunCrosshairCuts none = gunCrosshairCuts({}, {{10, 400, 64, 70}});
    CHECK(none.hud.empty());
    CHECK(none.moved.empty());
}

TEST_CASE("gun crosshair: a marker in the middle stays on the HUD and leaves the moved image") {
    const PixelRect r = gunCrosshairRect(1280, 1400);
    // One box wholly inside, one across the rectangle's left edge.
    const PixelRect in{r.x + 40, r.y + 30, 64, 70};
    const PixelRect edge{r.x - 20, r.y + 100, 64, 70};
    const evr::ui_layer::GunCrosshairCuts cuts = gunCrosshairCuts(r, {in, edge});
    REQUIRE(cuts.moved.size() == 2);
    // In the moved image's own pixels: the inner box as it is, the edge box from the image's left edge.
    CHECK(cuts.moved[0].x == 40);
    CHECK(cuts.moved[0].y == 30);
    CHECK(cuts.moved[0].width == 64);
    CHECK(cuts.moved[1].x == 0);
    CHECK(cuts.moved[1].y == 100);
    CHECK(cuts.moved[1].width == 44);
    CHECK(cuts.moved[1].height == 70);
    // The HUD keeps exactly the boxes' parts: the cleared pieces and the boxes tile the rectangle.
    CHECK(area(cuts.hud) + 64 * 70 + 44 * 70 == area(r));
    for (const PixelRect& piece : cuts.hud) {
        CHECK(inside(piece, r));
        CHECK_FALSE(overlaps(piece, in));
        CHECK_FALSE(overlaps(piece, edge));
    }
    for (std::size_t i = 0; i < cuts.hud.size(); ++i) {
        for (std::size_t j = i + 1; j < cuts.hud.size(); ++j) {
            CHECK_FALSE(overlaps(cuts.hud[i], cuts.hud[j]));
        }
    }
}

TEST_CASE("gun crosshair: the quad spans the angle the rectangle spans on the HUD, at any distance") {
    const PixelRect r{0, 0, 259, 216};
    // A 1.2 m HUD 1.0 m ahead showing 1280 px: 259 px span 2 atan(0.1214 / 1) there.
    const auto near = gunCrosshairSize(r, 1280, 1.2f, 1.0f, 1.0f);
    CHECK(near.width == doctest::Approx(259.0 * 1.2 / 1280.0));
    CHECK(near.height == doctest::Approx(216.0 * 1.2 / 1280.0));
    const auto far = gunCrosshairSize(r, 1280, 1.2f, 1.0f, 25.0f);
    CHECK(far.width == doctest::Approx(near.width * 25.0));
    CHECK(std::atan(far.width / 2.0 / 25.0) == doctest::Approx(std::atan(near.width / 2.0)));
    // The aspect is the rectangle's.
    CHECK(far.width / far.height == doctest::Approx(259.0 / 216.0));
    // Nothing for an empty input.
    CHECK(gunCrosshairSize({}, 1280, 1.2f, 1.0f, 10.0f).width == 0.0f);
    CHECK(gunCrosshairSize(r, 0, 1.2f, 1.0f, 10.0f).width == 0.0f);
    CHECK(gunCrosshairSize(r, 1280, 0.0f, 1.0f, 10.0f).width == 0.0f);
    CHECK(gunCrosshairSize(r, 1280, 1.2f, 0.0f, 10.0f).width == 0.0f);
    CHECK(gunCrosshairSize(r, 1280, 1.2f, 1.0f, 0.0f).width == 0.0f);
}

TEST_CASE("gun crosshair: tiny and odd targets give a rectangle inside the band, or none") {
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes{
        {1, 1}, {2, 1}, {3, 5}, {16, 9}, {1281, 1401}, {1279, 721}, {4097, 3}};
    for (const auto& [w, h] : sizes) {
        const PixelRect band = evr::ui_layer::wideContentRect(w, h);
        const PixelRect r = gunCrosshairRect(w, h);
        if (band.width == 0 || band.height == 0) {
            CHECK(area(r) == 0);
            continue;
        }
        CHECK(inside(r, band));
        if (area(r) > 0) {
            checkCentred(r, band);
        }
    }
    CHECK(area(gunCrosshairRect(0, 1400)) == 0);
}

TEST_CASE("gun crosshair: a 21:9 target keeps the rectangle in its 16:9 band") {
    // 2560 x 1080: a 1920 x 1080 band with 320 empty columns each side.
    const PixelRect band = evr::ui_layer::wideContentRect(2560, 1080);
    CHECK(band.width == 1920);
    CHECK(band.x == 320);
    const PixelRect r = gunCrosshairRect(2560, 1080);
    CHECK(r.width == static_cast<std::uint32_t>(std::lround(kGunCrosshairWidth * 1080.0f)));
    CHECK(inside(r, band));
    checkCentred(r, band);
}

TEST_CASE("gun crosshair: markers partly outside, several, overlapping and covering all") {
    const PixelRect r = gunCrosshairRect(1280, 1400);
    const auto w = static_cast<std::int32_t>(r.width);
    const auto h = static_cast<std::int32_t>(r.height);
    // Partly outside on each side and at a corner; one inside; two overlapping each other; one wholly out.
    const std::vector<PixelRect> boxes{{r.x - 30, r.y + 10, 50, 40},   {r.x + w - 25, r.y + 60, 50, 40},
                                       {r.x + 100, r.y - 20, 40, 50},  {r.x + 200, r.y + h - 10, 40, 50},
                                       {r.x - 10, r.y - 10, 30, 30},   {r.x + 300, r.y + 100, 60, 60},
                                       {r.x + 330, r.y + 120, 60, 60}, {r.x + 2000, r.y, 10, 10}};
    const evr::ui_layer::GunCrosshairCuts cuts = gunCrosshairCuts(r, boxes);
    CHECK(cuts.moved.size() == 7);
    for (const PixelRect& m : cuts.moved) {
        CHECK(inside(m, {0, 0, r.width, r.height})); // in the moved image's own pixels
    }
    // The HUD's pieces stay in the rectangle, apart from each other and from every box.
    for (const PixelRect& piece : cuts.hud) {
        CHECK(inside(piece, r));
        for (const PixelRect& b : boxes) {
            CHECK_FALSE(overlaps(piece, b));
        }
    }
    for (std::size_t i = 0; i < cuts.hud.size(); ++i) {
        for (std::size_t j = i + 1; j < cuts.hud.size(); ++j) {
            CHECK_FALSE(overlaps(cuts.hud[i], cuts.hud[j]));
        }
    }
    // A box over the whole rectangle: nothing cleared from the HUD, the whole moved image cleared.
    const evr::ui_layer::GunCrosshairCuts all =
        gunCrosshairCuts(r, {{r.x - 5, r.y - 5, r.width + 10, r.height + 10}});
    CHECK(all.hud.empty());
    REQUIRE(all.moved.size() == 1);
    CHECK(area(all.moved[0]) == area(r));
}

TEST_CASE("gun crosshair: the size is nothing for zero, negative or non-finite inputs") {
    const PixelRect r{0, 0, 259, 216};
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(gunCrosshairSize(r, 1280, -1.2f, 1.0f, 10.0f).width == 0.0f);
    CHECK(gunCrosshairSize(r, 1280, 1.2f, -1.0f, 10.0f).width == 0.0f);
    CHECK(gunCrosshairSize(r, 1280, 1.2f, 1.0f, -10.0f).width == 0.0f);
    CHECK(gunCrosshairSize(r, 1280, nan, 1.0f, 10.0f).width == 0.0f);
    CHECK(gunCrosshairSize(r, 1280, 1.2f, inf, 10.0f).width == 0.0f);
    CHECK(gunCrosshairSize(r, 1280, 1.2f, 1.0f, inf).height == 0.0f);
    CHECK(gunCrosshairSize({0, 0, 259, 0}, 1280, 1.2f, 1.0f, 10.0f).width == 0.0f);
}

TEST_CASE("gun crosshair: the HUD's image rect sets the angle (the 16:9 crop or the whole target)") {
    // ETERNALVR_UI_CROP on, a 21:9 target: the HUD shows its 1920 px band over its width; off: all 2560 px,
    // so a pixel spans less of the HUD and the moved quad is smaller by that ratio.
    const PixelRect r = gunCrosshairRect(2560, 1080);
    const auto cropped =
        gunCrosshairSize(r, evr::ui_layer::wideContentRect(2560, 1080).width, 1.2f, 1.0f, 10.0f);
    const auto whole = gunCrosshairSize(r, 2560, 1.2f, 1.0f, 10.0f);
    CHECK(cropped.width == doctest::Approx(whole.width * 2560.0 / 1920.0));
    CHECK(cropped.width / cropped.height == doctest::Approx(whole.width / whole.height));
}

TEST_CASE("gun crosshair: the border test's points ring the rectangle, inside the target") {
    using evr::ui_layer::gunCrosshairBorderPoints;
    const PixelRect r = gunCrosshairRect(1280, 1400);
    const auto p = gunCrosshairBorderPoints(r, 1280, 1400, 4);
    REQUIRE(p);
    for (const auto& pt : *p) {
        CHECK(pt.x < 1280);
        CHECK(pt.y < 1400);
        // Outside the rectangle along at least one axis (the edge midpoints are on its middle lines).
        const bool outX = pt.x < static_cast<std::uint32_t>(r.x) || pt.x >= r.x + r.width;
        const bool outY = pt.y < static_cast<std::uint32_t>(r.y) || pt.y >= r.y + r.height;
        CHECK((outX || outY));
    }
    CHECK((*p)[0].x == static_cast<std::uint32_t>(r.x - 4));
    CHECK((*p)[0].y == static_cast<std::uint32_t>(r.y - 4));
    CHECK((*p)[7].x == r.x + r.width + 3);
    CHECK((*p)[7].y == r.y + r.height + 3);
    // A rectangle at the target's edges: the points are kept inside it.
    const auto edge = gunCrosshairBorderPoints({0, 0, 16, 9}, 16, 9, 4);
    REQUIRE(edge);
    for (const auto& pt : *edge) {
        CHECK(pt.x < 16);
        CHECK(pt.y < 9);
    }
    CHECK_FALSE(gunCrosshairBorderPoints({}, 1280, 1400, 4));
    CHECK_FALSE(gunCrosshairBorderPoints(r, 0, 0, 4));
}

TEST_CASE("gun crosshair: the border test keeps a copy whole only for a full-screen layer") {
    using evr::ui_layer::readingShowsBackdrop;
    std::array<std::uint8_t, evr::ui_layer::kBackdropProbePoints * 4> rgba{};
    CHECK_FALSE(readingShowsBackdrop(rgba)); // the game around the crosshair: nothing drawn
    rgba[6 * 4 + 3] = 255;                   // the low ammo banner's glow under the rectangle: one point
    CHECK_FALSE(readingShowsBackdrop(rgba));
    for (std::size_t i = 0; i < evr::ui_layer::kBackdropProbePoints; ++i) {
        rgba[i * 4 + 3] = 200; // a pause menu's backdrop, a death fade on its way in
    }
    CHECK(readingShowsBackdrop(rgba));
}

TEST_CASE("gun crosshair: the Performance Metrics quad keeps its place unless the gun's quad shows") {
    using evr::ui_layer::layersAfterPerfQuad;
    using evr::ui_layer::perfQuadFits;
    // A full HUD in pieces: the projection, the vignette and 11 HUD quads before the metrics' quad.
    constexpr std::uint32_t kFullHud = 1 + 1 + 11;
    // hold, or gun without a moved image this frame: the dot and the fade after it, as before.
    CHECK(layersAfterPerfQuad(false, false) == 2);
    CHECK(perfQuadFits(kFullHud, layersAfterPerfQuad(false, false)));
    // gun with the quad up: one more after it, so the metrics give way, and the rest still fits.
    CHECK(layersAfterPerfQuad(false, true) == 3);
    CHECK_FALSE(perfQuadFits(kFullHud, layersAfterPerfQuad(false, true)));
    CHECK(kFullHud + layersAfterPerfQuad(false, true) <= evr::ui_layer::kMaxFrameLayers);
    // The HUD as one quad with its markers' two quads: room for all of it either way.
    CHECK(layersAfterPerfQuad(true, true) == 5);
    CHECK(perfQuadFits(3, layersAfterPerfQuad(true, true)));
}
