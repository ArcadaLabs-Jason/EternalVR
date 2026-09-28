#include "ui_layer/hud_regions.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

using evr::ui_layer::BandRect;
using evr::ui_layer::cutRect;
using evr::ui_layer::panelPiece;
using evr::ui_layer::PixelRect;
using evr::ui_layer::subtractRects;
using evr::ui_layer::toPixels;
using evr::ui_layer::wideContentRect;
using evr::ui_layer::WristBlock;
using evr::ui_layer::wristBlockRect;

namespace {

bool contains(const PixelRect& outer, const PixelRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + static_cast<std::int64_t>(inner.width) <=
               outer.x + static_cast<std::int64_t>(outer.width) &&
           inner.y + static_cast<std::int64_t>(inner.height) <=
               outer.y + static_cast<std::int64_t>(outer.height);
}

std::uint64_t area(const PixelRect& r) {
    return static_cast<std::uint64_t>(r.width) * r.height;
}

bool overlap(const PixelRect& a, const PixelRect& b) {
    return a.x < b.x + static_cast<std::int64_t>(b.width) && b.x < a.x + static_cast<std::int64_t>(a.width) &&
           a.y < b.y + static_cast<std::int64_t>(b.height) && b.y < a.y + static_cast<std::int64_t>(a.height);
}

// A block measured on a rig capture (band coordinates), in pixels of that capture.
PixelRect measured(float x0, float x1, float y0, float y1, std::uint32_t w, std::uint32_t h) {
    return toPixels(BandRect{x0, y0, x1, y1}, w, h);
}

} // namespace

TEST_CASE("hud regions: every block lies inside the 16:9 band the UI quad shows") {
    // The render size's near-square eye images (docs/rig-findings/render-size.md), the old window sizes and
    // a plain 16:9 target.
    const std::uint32_t sizes[][2] = {{1280, 1400}, {2064, 2100}, {2560, 2100}, {2054, 2068}, {1920, 1080}};
    for (const auto& size : sizes) {
        const std::uint32_t w = size[0];
        const std::uint32_t h = size[1];
        CAPTURE(w);
        CAPTURE(h);
        const PixelRect band = wideContentRect(w, h);
        for (const WristBlock b : {WristBlock::Vitals, WristBlock::Weapon, WristBlock::Abilities}) {
            const PixelRect r = toPixels(wristBlockRect(b), w, h);
            CHECK(area(r) > 0);
            CHECK(contains(band, r));
        }
        for (const WristBlock b : {WristBlock::Vitals, WristBlock::Weapon}) {
            CHECK(contains(band, cutRect(b, w, h)));
        }
    }
}

TEST_CASE("hud regions: the blocks on a 1280x1400 eye image") {
    // The band: 1280 x 720 from row 340.
    const PixelRect band = wideContentRect(1280, 1400);
    REQUIRE(band.y == 340);
    REQUIRE(band.height == 720);
    const PixelRect vitals = toPixels(wristBlockRect(WristBlock::Vitals), 1280, 1400);
    CHECK(vitals.x == 0);
    CHECK(vitals.width == 276);                                         // ceil(0.215 * 1280)
    CHECK(vitals.y == 887);                                             // floor(340 + 0.76 * 720)
    CHECK(vitals.y + static_cast<std::int64_t>(vitals.height) == 1060); // the band's bottom, not 1400
    const PixelRect weapon = toPixels(wristBlockRect(WristBlock::Weapon), 1280, 1400);
    CHECK(weapon.x == 979); // floor(0.765 * 1280)
    CHECK(weapon.x + static_cast<std::int64_t>(weapon.width) == 1280);
    const PixelRect abilities = toPixels(wristBlockRect(WristBlock::Abilities), 1280, 1400);
    CHECK(abilities.y == 649);                                               // floor(340 + 0.43 * 720)
    CHECK(abilities.y + static_cast<std::int64_t>(abilities.height) == 765); // ceil(340 + 0.59 * 720)
}

TEST_CASE("hud regions: block rectangles in pixels") {
    const PixelRect vitals = toPixels(wristBlockRect(WristBlock::Vitals), 2560, 2100);
    CHECK(vitals.x == 0);
    CHECK(vitals.width == 551); // ceil(0.215 * 2560)
    CHECK(vitals.y == 1424);    // floor(330 + 0.76 * 1440)
    CHECK(vitals.y + static_cast<std::int64_t>(vitals.height) == 1770);
    const PixelRect weapon = toPixels(wristBlockRect(WristBlock::Weapon), 2560, 2100);
    CHECK(weapon.x + static_cast<std::int64_t>(weapon.width) == 2560);
    CHECK(toPixels(BandRect{1.2f, 0.0f, 1.5f, 1.0f}, 100, 100).width == 0); // outside
}

TEST_CASE("hud regions: the blocks cover what the rig's captures show, at both window sizes") {
    // (tmp-vr u6b-ui c002400 at 2560x2100, u6c-ui c012000 at 2054x2068; band coordinates measured with a
    // small dilation, see docs/VR_HANDS_HUD.md.)
    struct Capture {
        std::uint32_t w, h;
        float vitals[4], weapon[4], abilities[4];
    };
    const Capture captures[] = {
        {2560,
         2100,
         {0.0219f, 0.1969f, 0.8375f, 0.9597f},
         {0.7812f, 0.9437f, 0.8153f, 0.9486f},
         {0.4062f, 0.5906f, 0.4486f, 0.5708f}},
        {2054,
         2068,
         {0.0195f, 0.1947f, 0.8376f, 0.9622f},
         {0.7790f, 0.9426f, 0.8099f, 0.9553f},
         {0.4051f, 0.5920f, 0.4429f, 0.5744f}},
    };
    for (const Capture& c : captures) {
        CAPTURE(c.w);
        const auto m = [&c](const float r[4]) {
            return measured(r[0], r[1], r[2], r[3], c.w, c.h);
        };
        CHECK(contains(toPixels(wristBlockRect(WristBlock::Vitals), c.w, c.h), m(c.vitals)));
        CHECK(contains(toPixels(wristBlockRect(WristBlock::Weapon), c.w, c.h), m(c.weapon)));
        CHECK(contains(toPixels(wristBlockRect(WristBlock::Abilities), c.w, c.h), m(c.abilities)));
        // The top bars and the objective marker on the right are not taken.
        const PixelRect topBar = measured(0.3844f, 0.6156f, 0.0264f, 0.0986f, c.w, c.h);
        const PixelRect marker = measured(0.9031f, 0.9469f, 0.4653f, 0.5597f, c.w, c.h);
        for (const WristBlock b : {WristBlock::Vitals, WristBlock::Weapon}) {
            CHECK_FALSE(overlap(cutRect(b, c.w, c.h), topBar));
            CHECK_FALSE(overlap(cutRect(b, c.w, c.h), marker));
        }
    }
}

TEST_CASE("hud regions: corner cuts reach the band's edges; the centre is never cut") {
    // The band of 2560x2100: 2560 x 1440 from row 330.
    const PixelRect v = cutRect(WristBlock::Vitals, 2560, 2100);
    CHECK(v.x == 0);
    CHECK(v.y + static_cast<std::int64_t>(v.height) == 1770);
    const PixelRect w = cutRect(WristBlock::Weapon, 2560, 2100);
    CHECK(w.x + static_cast<std::int64_t>(w.width) == 2560);
    CHECK(w.y + static_cast<std::int64_t>(w.height) == 1770);
    CHECK(area(cutRect(WristBlock::Abilities, 2560, 2100)) == 0);
}

TEST_CASE("hud regions: subtracting two bottom corners leaves a top band and a bottom middle") {
    const auto pieces = subtractRects({0, 0, 100, 100}, {{0, 80, 20, 20}, {70, 80, 30, 20}});
    REQUIRE(pieces.size() == 2);
    CHECK(pieces[0].x == 0);
    CHECK(pieces[0].y == 0);
    CHECK(pieces[0].width == 100);
    CHECK(pieces[0].height == 80);
    CHECK(pieces[1].x == 20);
    CHECK(pieces[1].y == 80);
    CHECK(pieces[1].width == 50);
    CHECK(pieces[1].height == 20);
}

TEST_CASE("hud regions: subtraction is disjoint and keeps exactly the uncut area") {
    const PixelRect whole{0, 0, 97, 61};
    const std::vector<std::vector<PixelRect>> cases = {
        {},
        {{10, 10, 20, 20}},
        {{-5, -5, 30, 30}, {50, 40, 100, 100}},
        {{10, 10, 20, 20}, {15, 15, 20, 20}}, // overlapping cuts
        {{0, 0, 97, 61}},                     // everything
        {{20, 0, 10, 61}, {0, 30, 97, 5}, {0, 0, 0, 0}},
    };
    for (const auto& cuts : cases) {
        CAPTURE(cuts.size());
        const auto pieces = subtractRects(whole, cuts);
        std::uint64_t kept = 0;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            CHECK(area(pieces[i]) > 0);
            CHECK(contains(whole, pieces[i]));
            for (const PixelRect& c : cuts) {
                CHECK_FALSE((area(c) > 0 && overlap(pieces[i], c)));
            }
            for (std::size_t j = i + 1; j < pieces.size(); ++j) {
                CHECK_FALSE(overlap(pieces[i], pieces[j]));
            }
            kept += area(pieces[i]);
        }
        // Count the uncut pixels directly.
        std::uint64_t expected = 0;
        for (std::int32_t y = 0; y < 61; ++y) {
            for (std::int32_t x = 0; x < 97; ++x) {
                bool cut = false;
                for (const PixelRect& c : cuts) {
                    cut = cut || (x >= c.x && x < c.x + static_cast<std::int64_t>(c.width) && y >= c.y &&
                                  y < c.y + static_cast<std::int64_t>(c.height));
                }
                expected += cut ? 0 : 1;
            }
        }
        CHECK(kept == expected);
    }
    CHECK(subtractRects({0, 0, 0, 10}, {}).empty());
}

TEST_CASE("hud regions: a piece's place on the head-locked quad") {
    const PixelRect shown{0, 0, 200, 100};
    const auto whole = panelPiece({0, 0, 200, 100}, shown, 2.0f);
    CHECK(whole.centreX == doctest::Approx(0.0f));
    CHECK(whole.centreY == doctest::Approx(0.0f));
    CHECK(whole.width == doctest::Approx(2.0f));
    CHECK(whole.height == doctest::Approx(1.0f));
    const auto bottomRight = panelPiece({100, 50, 100, 50}, shown, 2.0f);
    CHECK(bottomRight.centreX == doctest::Approx(0.5f));
    CHECK(bottomRight.centreY == doctest::Approx(-0.25f));
    CHECK(bottomRight.width == doctest::Approx(1.0f));
    CHECK(bottomRight.height == doctest::Approx(0.5f));
    CHECK(panelPiece({0, 0, 10, 10}, PixelRect{}, 2.0f).width == doctest::Approx(0.0f));
}

TEST_CASE("hud regions: pieces of the band sit where the band quad shows them") {
    // The UI quad shows the band (1280 x 720 from row 340) 2 m wide: the band is the whole quad, and its
    // top half is centred 0.28125 m above the quad's centre.
    const PixelRect band = wideContentRect(1280, 1400);
    const auto all = panelPiece(band, band, 2.0f);
    CHECK(all.centreX == doctest::Approx(0.0f));
    CHECK(all.centreY == doctest::Approx(0.0f));
    CHECK(all.width == doctest::Approx(2.0f));
    CHECK(all.height == doctest::Approx(1.125f));
    const auto top = panelPiece({0, 340, 1280, 360}, band, 2.0f);
    CHECK(top.centreY == doctest::Approx(0.28125f));
    CHECK(top.height == doctest::Approx(0.5625f));
}
