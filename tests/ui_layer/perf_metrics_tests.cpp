#include "ui_layer/hud_view.hpp"
#include "ui_layer/perf_metrics.hpp"
#include "ui_layer/ui_settings.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using evr::ui_layer::composeHudView;
using evr::ui_layer::describeHudView;
using evr::ui_layer::HudView;
using evr::ui_layer::perfMetricsQuad;
using evr::ui_layer::perfMetricsRect;
using evr::ui_layer::PixelRect;
using evr::ui_layer::readPerfOverlayLevel;
using evr::ui_layer::readUiSettings;
using evr::ui_layer::wideContentRect;

namespace {

evr::ui_layer::EnvLookup envOf(std::map<std::wstring, std::wstring> values) {
    return [values](std::wstring_view name) -> std::optional<std::wstring> {
        const auto it = values.find(std::wstring(name));
        if (it == values.end()) {
            return std::nullopt;
        }
        return it->second;
    };
}

bool contains(const PixelRect& outer, const PixelRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + static_cast<std::int64_t>(inner.width) <=
               outer.x + static_cast<std::int64_t>(outer.width) &&
           inner.y + static_cast<std::int64_t>(inner.height) <=
               outer.y + static_cast<std::int64_t>(outer.height);
}

bool overlap(const PixelRect& a, const PixelRect& b) {
    return a.x < b.x + static_cast<std::int64_t>(b.width) && b.x < a.x + static_cast<std::int64_t>(a.width) &&
           a.y < b.y + static_cast<std::int64_t>(b.height) && b.y < a.y + static_cast<std::int64_t>(a.height);
}

// What the game drew for its metrics (alpha 16 or more) in the rig's GUI captures at 2048 x 2176, Steam
// build 25216728, by com_showFPS level (index - 1).
struct Drawn {
    int level;
    PixelRect box;
};
constexpr Drawn kDrawn2048[] = {
    {1, {1867, 18, 180, 54}},  // "80fps - 12.38ms", "rs-off / 2048x2176", "Tick-Adaptive 81Hz"
    {2, {1867, 18, 181, 136}}, // level 1's lines and the average, HDR, RT, DLSS
    {3, {1898, 18, 149, 14}},  // "80fps - 12.25ms" alone
};

} // namespace

TEST_CASE("perf overlay: the level from ETERNALVR_PERF_OVERLAY") {
    std::vector<std::string> warnings;
    CHECK(readPerfOverlayLevel(envOf({}), warnings) == 0);
    CHECK(readPerfOverlayLevel(envOf({{L"ETERNALVR_PERF_OVERLAY", L"1"}}), warnings) == 1);
    CHECK(readPerfOverlayLevel(envOf({{L"ETERNALVR_PERF_OVERLAY", L" 3 "}}), warnings) == 3);
    CHECK(readPerfOverlayLevel(envOf({{L"ETERNALVR_PERF_OVERLAY", L"0"}}), warnings) == 0);
    CHECK(warnings.empty());
    for (const wchar_t* bad : {L"4", L"-1", L"high", L"", L"12"}) {
        std::vector<std::string> w;
        CHECK(readPerfOverlayLevel(envOf({{L"ETERNALVR_PERF_OVERLAY", bad}}), w) == 0);
        CHECK(w.size() == 1);
    }
    // Read with the UI layer's settings; off by default.
    CHECK(readUiSettings(envOf({}), warnings).perfOverlayLevel == 0);
    CHECK(readUiSettings(envOf({{L"ETERNALVR_PERF_OVERLAY", L"2"}}), warnings).perfOverlayLevel == 2);
}

TEST_CASE("perf overlay: the corner holds what the game drew, all of it above the HUD's band") {
    const PixelRect band = wideContentRect(2048, 2176);
    for (const Drawn& d : kDrawn2048) {
        CAPTURE(d.level);
        // The problem: the UI quad shows only the band, and the metrics are above it.
        CHECK_FALSE(overlap(band, d.box));
        const std::optional<PixelRect> corner = perfMetricsRect(d.level, 2048, 2176);
        REQUIRE(corner);
        CHECK(contains(*corner, d.box));
        CHECK(corner->x + static_cast<std::int64_t>(corner->width) == 2048);
        CHECK(corner->y == 0);
        CHECK_FALSE(overlap(band, *corner));
    }
    CHECK_FALSE(perfMetricsRect(0, 2048, 2176));
    CHECK_FALSE(perfMetricsRect(4, 2048, 2176));
    CHECK_FALSE(perfMetricsRect(1, 0, 2176));
    // A target narrower than the corner: all of its width.
    const std::optional<PixelRect> narrow = perfMetricsRect(1, 200, 400);
    REQUIRE(narrow);
    CHECK(narrow->x == 0);
    CHECK(narrow->width == 200);
}

TEST_CASE("perf overlay: its quad sits on the HUD panel's top edge, at its right edge and scale") {
    const PixelRect band = wideContentRect(2048, 2176); // rows 512..1663
    REQUIRE(band.y == 512);
    for (const Drawn& d : kDrawn2048) {
        CAPTURE(d.level);
        const auto q = perfMetricsQuad(d.level, 2048, 2176, band, 2.0f);
        REQUIRE(q);
        CHECK(contains(q->source, d.box));
        const float metresPerPixel = 2.0f / 2048.0f;
        CHECK(q->width == doctest::Approx(static_cast<float>(q->source.width) * metresPerPixel));
        CHECK(q->height == doctest::Approx(static_cast<float>(q->source.height) * metresPerPixel));
        // Right edge on the panel's right edge (1 m right of its centre), bottom edge on its top edge.
        CHECK(q->centreX + q->width / 2.0f == doctest::Approx(1.0f));
        CHECK(q->centreY - q->height / 2.0f == doctest::Approx(1152.0f * metresPerPixel / 2.0f));
        // The metrics' first line ends up just above the panel: within its own quad's height.
        CHECK(q->height < 0.25f);
    }
}

TEST_CASE("perf overlay: the game's lines keep their size in pixels; the quad keeps their angle on larger "
          "targets") {
    // The rig's 1448 x 1560 capture at level 2: the same lines in the same pixels as at 2048 x 2176.
    const PixelRect drawn1448{1267, 18, 181, 136};
    const PixelRect band1448 = wideContentRect(1448, 1560);
    const auto small = perfMetricsQuad(2, 1448, 1560, band1448, 2.0f);
    REQUIRE(small);
    CHECK(contains(small->source, drawn1448));
    // A smaller band: the UI quad's own scale, so the lines look larger there.
    CHECK(small->width == doctest::Approx(320.0f * 2.0f / 1448.0f));
    // A larger band (a 2768 x 2952 target): the scale of a 2048-wide band, not the UI quad's.
    const PixelRect band2768 = wideContentRect(2768, 2952);
    const auto large = perfMetricsQuad(2, 2768, 2952, band2768, 2.0f);
    REQUIRE(large);
    CHECK(large->width == doctest::Approx(320.0f * 2.0f / 2048.0f));
    CHECK(large->height == doctest::Approx(172.0f * 2.0f / 2048.0f));
    CHECK(large->centreX + large->width / 2.0f == doctest::Approx(1.0f));
    CHECK(large->centreY - large->height / 2.0f ==
          doctest::Approx(static_cast<float>(band2768.height) * 2.0f / 2768.0f / 2.0f));
}

TEST_CASE("perf overlay: no quad where the UI quad shows the corner already, never band rows twice") {
    // Off, an empty panel, a 16:9 target (its band is the whole target), or ETERNALVR_UI_CROP=0.
    CHECK_FALSE(perfMetricsQuad(0, 2048, 2176, wideContentRect(2048, 2176), 2.0f));
    CHECK_FALSE(perfMetricsQuad(1, 2048, 2176, wideContentRect(2048, 2176), 0.0f));
    CHECK_FALSE(perfMetricsQuad(1, 1920, 1080, wideContentRect(1920, 1080), 2.0f));
    CHECK_FALSE(perfMetricsQuad(1, 2048, 2176, PixelRect{0, 0, 2048, 2176}, 2.0f));
    // Wider than 16:9: the band is columns, the corner is outside them and not shown (as before).
    CHECK_FALSE(perfMetricsQuad(1, 2560, 1080, wideContentRect(2560, 1080), 2.0f));
    // Only a little taller than 16:9: the corner keeps to the rows above the band.
    const PixelRect band = wideContentRect(2048, 1200);
    REQUIRE(band.y > 0);
    const auto q = perfMetricsQuad(2, 2048, 1200, band, 2.0f);
    REQUIRE(q);
    CHECK(q->source.height == static_cast<std::uint32_t>(band.y));
    CHECK_FALSE(overlap(q->source, band));
}

TEST_CASE("perf overlay: the HUD capture draws the metrics above the panel, where the headset shows them") {
    // A GUI target holding only what the game drew for its level 2 metrics, opaque.
    const std::uint32_t w = 2048;
    const std::uint32_t h = 2176;
    const PixelRect box = kDrawn2048[1].box;
    std::vector<std::uint8_t> rgba(std::size_t{w} * h * 4, 0);
    for (std::uint32_t y = 0; y < box.height; ++y) {
        for (std::uint32_t x = 0; x < box.width; ++x) {
            const std::size_t at = (std::size_t{box.y + y} * w + box.x + x) * 4;
            std::fill_n(rgba.begin() + static_cast<std::ptrdiff_t>(at), 4, std::uint8_t{255});
        }
    }
    const auto opaqueRows = [](const evr::ui_layer::HudComposition& c) {
        std::vector<std::uint32_t> rows;
        for (std::uint32_t y = 0; y < c.image.height; ++y) {
            for (std::uint32_t x = 0; x < c.image.width; ++x) {
                if (c.image.rgba[(std::size_t{y} * c.image.width + x) * 4 + 3] > 128) {
                    rows.push_back(y);
                    break;
                }
            }
        }
        return rows;
    };
    HudView view;
    view.shown = wideContentRect(w, h);
    view.panelWidth = 2.0f;
    view.pieces = {view.shown};
    // Without the metrics' quad (as before) nothing the game drew for them is seen.
    const auto before = composeHudView(rgba.data(), w, h, view);
    CHECK(before.image.height == view.shown.height);
    CHECK(opaqueRows(before).empty());
    const auto q = perfMetricsQuad(2, w, h, view.shown, view.panelWidth);
    REQUIRE(q);
    view.metrics = q->source;
    view.metricsWidth = q->width;
    view.metricsHeight = q->height;
    const auto c = composeHudView(rgba.data(), w, h, view);
    CHECK(c.panelY == q->source.height);
    CHECK(c.image.height == view.shown.height + q->source.height);
    const std::vector<std::uint32_t> rows = opaqueRows(c);
    REQUIRE(rows.size() == box.height);
    // Above the panel, at the rows the game drew them in within the corner, right-aligned.
    CHECK(rows.front() == static_cast<std::uint32_t>(box.y));
    CHECK(rows.back() < c.panelY);
    CHECK(c.image.rgba[(std::size_t{rows.front()} * c.image.width + (w - 1)) * 4 + 3] == 255);
    CHECK(describeHudView(view).find("Performance Metrics above the UI quad's top-right corner") !=
          std::string::npos);
}

TEST_CASE("perf overlay: on a larger target the HUD capture draws the metrics' quad scaled up") {
    const std::uint32_t w = 2768;
    const std::uint32_t h = 2952;
    const PixelRect box{w - 181, 18, 181, 136};
    std::vector<std::uint8_t> rgba(std::size_t{w} * h * 4, 0);
    for (std::uint32_t y = 0; y < box.height; ++y) {
        for (std::uint32_t x = 0; x < box.width; ++x) {
            const std::size_t at = (std::size_t{box.y + y} * w + box.x + x) * 4;
            std::fill_n(rgba.begin() + static_cast<std::ptrdiff_t>(at), 4, std::uint8_t{255});
        }
    }
    HudView view;
    view.shown = wideContentRect(w, h);
    view.panelWidth = 2.0f;
    view.pieces = {view.shown};
    const auto q = perfMetricsQuad(2, w, h, view.shown, view.panelWidth);
    REQUIRE(q);
    view.metrics = q->source;
    view.metricsWidth = q->width;
    view.metricsHeight = q->height;
    const auto c = composeHudView(rgba.data(), w, h, view);
    // 2768 / 2048 times the pixels: 172 rows of the corner become 232 or 233 above the panel.
    CHECK(c.panelY >= 232);
    CHECK(c.panelY <= 233);
    std::uint32_t first = c.image.height;
    std::uint32_t last = 0;
    for (std::uint32_t y = 0; y < c.image.height; ++y) {
        for (std::uint32_t x = 0; x < c.image.width; ++x) {
            if (c.image.rgba[(std::size_t{y} * c.image.width + x) * 4 + 3] > 128) {
                first = std::min(first, y);
                last = std::max(last, y);
                break;
            }
        }
    }
    REQUIRE(last >= first);
    CHECK(last < c.panelY);
    CHECK(last - first + 1 >= 182);
    CHECK(last - first + 1 <= 185);
}

TEST_CASE("perf overlay: the quad only goes into a frame with room for it under OpenXR's 16 layers") {
    using evr::ui_layer::kMaxFrameLayers;
    using evr::ui_layer::perfQuadFits;
    REQUIRE(kMaxFrameLayers == 16);
    // The fullest frames the layer builds with it: the projection, the vignette and 11 HUD quads (pieces,
    // hand quads, the wheel) before it, the reticle and the fade after it; or, with the markers' quads, the
    // projection, the vignette and the whole UI quad before it, the two marker quads, the reticle and the
    // fade after it.
    CHECK(perfQuadFits(13, 2));
    CHECK(perfQuadFits(3, 4));
    CHECK(perfQuadFits(15, 0));
    CHECK(perfQuadFits(0, 15));
    CHECK_FALSE(perfQuadFits(14, 2));
    CHECK_FALSE(perfQuadFits(16, 0));
    CHECK_FALSE(perfQuadFits(0, 16));
    CHECK_FALSE(perfQuadFits(1, 0xFFFFFFFFu));
    CHECK_FALSE(perfQuadFits(0xFFFFFFFFu, 1));
}

TEST_CASE("perf overlay: no quad while a full-screen backdrop (a fade, a menu's backdrop) fills the corner") {
    using evr::ui_layer::perfLevelShown;
    // The rig's GUI captures at an immersive cutscene's start: the whole target opaque black, the corner too,
    // while the backdrop test read a full-screen backdrop; the quad would stand above the panel as a black
    // rectangle.
    CHECK(perfLevelShown(2, true) == 0);
    CHECK(perfLevelShown(3, true) == 0);
    CHECK(perfLevelShown(2, false) == 2);
    CHECK(perfLevelShown(3, false) == 3);
    CHECK(perfLevelShown(0, false) == 0);
    CHECK_FALSE(perfMetricsQuad(perfLevelShown(2, true), 2048, 2176, wideContentRect(2048, 2176), 2.0f));
}
