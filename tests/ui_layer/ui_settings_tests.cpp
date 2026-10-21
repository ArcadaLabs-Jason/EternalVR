#include "ui_layer/ui_settings.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using evr::ui_layer::copyRegionsWithoutCentre;
using evr::ui_layer::panelSize;
using evr::ui_layer::readUiSettings;
using evr::ui_layer::reticleImage;
using evr::ui_layer::reticleSideMetres;

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

} // namespace

TEST_CASE("ui settings: off by default, defaults for the quad") {
    std::vector<std::string> warnings;
    const auto s = readUiSettings(envOf({}), warnings);
    CHECK_FALSE(s.enabled);
    CHECK(s.skipComposite);
    CHECK(s.distanceMetres == doctest::Approx(1.5f));
    CHECK(s.widthMetres == doctest::Approx(2.0f));
    CHECK(s.offsetYMetres == doctest::Approx(0.0f));
    CHECK(warnings.empty());
}

TEST_CASE("ui settings: every variable read") {
    std::vector<std::string> warnings;
    const auto s = readUiSettings(envOf({{L"ETERNALVR_UI_LAYER", L"1"},
                                         {L"ETERNALVR_UI_SKIP_COMPOSITE", L" 0 "},
                                         {L"ETERNALVR_UI_DISTANCE", L"2.25"},
                                         {L"ETERNALVR_UI_WIDTH", L"1.2"},
                                         {L"ETERNALVR_UI_OFFSET_Y", L"-0.3"}}),
                                  warnings);
    CHECK(s.enabled);
    CHECK_FALSE(s.skipComposite);
    CHECK(s.distanceMetres == doctest::Approx(2.25f));
    CHECK(s.widthMetres == doctest::Approx(1.2f));
    CHECK(s.offsetYMetres == doctest::Approx(-0.3f));
    CHECK(warnings.empty());
}

TEST_CASE("ui settings: bad values keep the defaults and warn") {
    std::vector<std::string> warnings;
    const auto s = readUiSettings(envOf({{L"ETERNALVR_UI_LAYER", L"yes"},
                                         {L"ETERNALVR_UI_DISTANCE", L"0.1"},
                                         {L"ETERNALVR_UI_WIDTH", L"wide"},
                                         {L"ETERNALVR_UI_OFFSET_Y", L"nan"}}),
                                  warnings);
    CHECK_FALSE(s.enabled);
    CHECK(s.distanceMetres == doctest::Approx(1.5f));
    CHECK(s.widthMetres == doctest::Approx(2.0f));
    CHECK(s.offsetYMetres == doctest::Approx(0.0f));
    CHECK(warnings.size() == 4);
}

TEST_CASE("ui settings: panel size follows the target's aspect") {
    const auto wide = panelSize(2.0f, 2560, 1440);
    REQUIRE(wide.has_value());
    CHECK(wide->width == doctest::Approx(2.0f));
    CHECK(wide->height == doctest::Approx(1.125f));
    const auto square = panelSize(1.5f, 2064, 2100);
    REQUIRE(square.has_value());
    CHECK(square->height == doctest::Approx(1.5f * 2100.0f / 2064.0f));
    CHECK_FALSE(panelSize(2.0f, 0, 1440).has_value());
    CHECK_FALSE(panelSize(0.0f, 2560, 1440).has_value());
}

TEST_CASE("ui settings: reticle defaults and variables") {
    std::vector<std::string> warnings;
    const auto d = readUiSettings(envOf({}), warnings);
    CHECK(d.reticle);
    CHECK(d.reticleDistanceMetres == doctest::Approx(10.0f));
    CHECK(d.reticleDegrees == doctest::Approx(1.0f));
    const auto s = readUiSettings(envOf({{L"ETERNALVR_UI_RETICLE", L"0"},
                                         {L"ETERNALVR_UI_RETICLE_DISTANCE", L"25"},
                                         {L"ETERNALVR_UI_RETICLE_SIZE", L"2"}}),
                                  warnings);
    CHECK_FALSE(s.reticle);
    CHECK(s.reticleDistanceMetres == doctest::Approx(25.0f));
    CHECK(s.reticleDegrees == doctest::Approx(2.0f));
    CHECK(warnings.empty());
}

TEST_CASE("reticle: the quad keeps its angular size") {
    CHECK(reticleSideMetres(10.0f, 1.0f) == doctest::Approx(0.17453f).epsilon(0.001));
    CHECK(reticleSideMetres(20.0f, 1.0f) == doctest::Approx(2.0f * reticleSideMetres(10.0f, 1.0f)));
}

TEST_CASE("reticle: premultiplied dot, ring and clear corners") {
    const std::uint32_t n = 32;
    const auto px = reticleImage(n);
    REQUIRE(px.size() == n * n * 4);
    const auto at = [&](std::uint32_t x, std::uint32_t y) {
        return px.data() + (y * n + x) * 4;
    };
    // Centre: opaque white.
    CHECK(at(16, 16)[3] == 255);
    CHECK(at(16, 16)[0] == 255);
    // Ring: dark and partly opaque.
    CHECK(at(16, 1)[0] == 0);
    CHECK(at(16, 1)[3] > 100);
    // Corner: transparent.
    CHECK(at(0, 0)[3] == 0);
    // Premultiplied everywhere: colour never exceeds alpha.
    for (std::size_t i = 0; i < px.size(); i += 4) {
        CHECK(px[i] <= px[i + 3]);
    }
}

TEST_CASE("centre mask: four regions cover everything but the centred square") {
    const std::uint32_t w = 2560;
    const std::uint32_t h = 2100;
    const auto regions = copyRegionsWithoutCentre(w, h, 0.1f);
    REQUIRE(regions.size() == 4);
    std::uint64_t area = 0;
    for (const auto& r : regions) {
        area += std::uint64_t{r.width} * r.height;
        CHECK(r.x >= 0);
        CHECK(r.y >= 0);
        CHECK(static_cast<std::uint32_t>(r.x) + r.width <= w);
        CHECK(static_cast<std::uint32_t>(r.y) + r.height <= h);
    }
    const std::uint64_t side = 210;
    CHECK(area == std::uint64_t{w} * h - side * side);
    // The centre pixel is in no region.
    for (const auto& r : regions) {
        const bool inside = 1280 >= r.x && 1280 < r.x + static_cast<std::int32_t>(r.width) && 1050 >= r.y &&
                            1050 < r.y + static_cast<std::int32_t>(r.height);
        CHECK_FALSE(inside);
    }
}

TEST_CASE("centre mask: no mask copies the whole target") {
    const auto regions = copyRegionsWithoutCentre(2560, 2100, 0.0f);
    REQUIRE(regions.size() == 1);
    CHECK(regions[0].width == 2560);
    CHECK(regions[0].height == 2100);
    CHECK(copyRegionsWithoutCentre(100, 100, 2.0f).size() == 1);
}

TEST_CASE("ui settings: on by default in Route S stereo only") {
    std::vector<std::string> warnings;
    CHECK(readUiSettings(envOf({{L"ETERNALVR_MODE", L"stereo"}}), warnings).enabled);
    CHECK(readUiSettings(envOf({{L"ETERNALVR_MODE", L"Stereo "}}), warnings).enabled);
    CHECK_FALSE(readUiSettings(envOf({{L"ETERNALVR_MODE", L"head"}}), warnings).enabled);
    CHECK_FALSE(readUiSettings(envOf({{L"ETERNALVR_MODE", L"cinema"}}), warnings).enabled);
    CHECK_FALSE(
        readUiSettings(envOf({{L"ETERNALVR_MODE", L"stereo"}, {L"ETERNALVR_STEREO_EXPERIMENT", L"left-eye"}}),
                       warnings)
            .enabled);
    CHECK_FALSE(
        readUiSettings(envOf({{L"ETERNALVR_MODE", L"stereo"}, {L"ETERNALVR_UI_LAYER", L"0"}}), warnings)
            .enabled);
    CHECK(readUiSettings(envOf({{L"ETERNALVR_MODE", L"head"}, {L"ETERNALVR_UI_LAYER", L"1"}}), warnings)
              .enabled);
    CHECK(warnings.empty());
}

TEST_CASE("ui settings: the menu panel follows the UI quad unless set") {
    std::vector<std::string> warnings;
    const auto defaults = readUiSettings(envOf({}), warnings);
    CHECK(defaults.menuPointer);
    CHECK(defaults.menuBeam);
    CHECK(defaults.menuDistanceMetres == doctest::Approx(defaults.distanceMetres));
    CHECK(defaults.menuWidthMetres == doctest::Approx(defaults.widthMetres));

    const auto quad = readUiSettings(
        envOf({{L"ETERNALVR_UI_DISTANCE", L"1.8"}, {L"ETERNALVR_UI_WIDTH", L"1.6"}}), warnings);
    CHECK(quad.menuDistanceMetres == doctest::Approx(1.8f));
    CHECK(quad.menuWidthMetres == doctest::Approx(1.6f));

    const auto own = readUiSettings(envOf({{L"ETERNALVR_UI_DISTANCE", L"1.8"},
                                           {L"ETERNALVR_MENU_DISTANCE", L"2.2"},
                                           {L"ETERNALVR_MENU_WIDTH", L"2.5"},
                                           {L"ETERNALVR_MENU_POINTER", L"0"},
                                           {L"ETERNALVR_MENU_BEAM", L"0"}}),
                                    warnings);
    CHECK_FALSE(own.menuPointer);
    CHECK_FALSE(own.menuBeam);
    CHECK(own.menuDistanceMetres == doctest::Approx(2.2f));
    CHECK(own.menuWidthMetres == doctest::Approx(2.5f));
    CHECK(warnings.empty());

    const auto bad = readUiSettings(
        envOf({{L"ETERNALVR_MENU_DISTANCE", L"50"}, {L"ETERNALVR_MENU_POINTER", L"yes"}}), warnings);
    CHECK(bad.menuPointer);
    CHECK(bad.menuDistanceMetres == doctest::Approx(bad.distanceMetres));
    CHECK(warnings.size() == 2);
}

TEST_CASE("wide content: the centred 16:9 band of a near-square target") {
    const auto band = evr::ui_layer::wideContentRect(2056, 2216);
    CHECK(band.x == 0);
    CHECK(band.width == 2056);
    CHECK(band.height == 1157); // 2056 * 9 / 16 = 1156.5
    CHECK(band.y == (2216 - 1157) / 2);
    const auto old = evr::ui_layer::wideContentRect(2064, 2100);
    CHECK(old.height == 1161);
    CHECK(old.y == 469);
}

TEST_CASE("wide content: a 16:9 target is shown whole") {
    // Within 0.5 % either way (rounding of window sizes).
    for (const auto& [w, h] :
         {std::pair<std::uint32_t, std::uint32_t>{1920, 1080}, {1280, 720}, {1281, 723}, {1286, 720}}) {
        const auto r = evr::ui_layer::wideContentRect(w, h);
        CHECK(r.x == 0);
        CHECK(r.y == 0);
        CHECK(r.width == w);
        CHECK(r.height == h);
    }
    const auto empty = evr::ui_layer::wideContentRect(0, 0);
    CHECK(empty.width == 0);
}

TEST_CASE("wide content: the centred 16:9 columns of a target wider than 16:9") {
    // A 2048x864 GUI capture (a 2560x1080 render size in the runtime's pixel budget) had the HUD's blocks and
    // the weapon wheel at the same fractions of its centred 1536 x 864 frame as a 2048x2208 capture of its
    // 2048 x 1152 band, to 0.002: idSWF fits its 16:9 frame inside the target either way.
    const auto band = evr::ui_layer::wideContentRect(2048, 864);
    CHECK(band.x == 256);
    CHECK(band.y == 0);
    CHECK(band.width == 1536);
    CHECK(band.height == 864);
    const auto ultra = evr::ui_layer::wideContentRect(2560, 1080);
    CHECK(ultra.x == 320);
    CHECK(ultra.width == 1920);
    CHECK(ultra.height == 1080);
    const auto odd = evr::ui_layer::wideContentRect(3441, 1440); // 2560 columns, 881 left over
    CHECK(odd.width == 2560);
    CHECK(odd.x == 440);
    // A point on the band's panel maps onto the whole image.
    const auto left = evr::ui_layer::contentToImage(0.0f, 0.5f, band, 2048, 864);
    CHECK(left.u == doctest::Approx(256.0 / 2048.0));
    CHECK(left.v == doctest::Approx(0.5));
    const auto right = evr::ui_layer::contentToImage(1.0f, 0.5f, band, 2048, 864);
    CHECK(right.u == doctest::Approx(1792.0 / 2048.0));
}

TEST_CASE("wide content: the menu panel's cut of a quad's part of an image") {
    // Eye R's half of a ring two eyes wide: its band, in the ring's pixels, columns and rows alike.
    const auto wide = evr::ui_layer::wideContentRectOf({2048, 0, 2048, 864});
    CHECK(wide.x == 2048 + 256);
    CHECK(wide.y == 0);
    CHECK(wide.width == 1536);
    CHECK(wide.height == 864);
    const auto tall = evr::ui_layer::wideContentRectOf({2056, 0, 2056, 2216});
    CHECK(tall.x == 2056);
    CHECK(tall.y == (2216 - 1157) / 2);
    CHECK(tall.width == 2056);
    CHECK(tall.height == 1157);
    // A band already cut stays as it is.
    const auto cut = evr::ui_layer::wideContentRectOf({256, 0, 1536, 864});
    CHECK(cut.x == 256);
    CHECK(cut.y == 0);
    CHECK(cut.width == 1536);
    CHECK(cut.height == 864);
}

TEST_CASE("wide content: a point on the band's panel maps onto the whole image") {
    const auto band = evr::ui_layer::wideContentRect(2064, 2100);
    const auto top = evr::ui_layer::contentToImage(0.0f, 0.0f, band, 2064, 2100);
    CHECK(top.u == doctest::Approx(0.0));
    CHECK(top.v == doctest::Approx(469.0 / 2100.0));
    const auto centre = evr::ui_layer::contentToImage(0.5f, 0.5f, band, 2064, 2100);
    CHECK(centre.u == doctest::Approx(0.5));
    CHECK(centre.v == doctest::Approx((469.0 + 580.5) / 2100.0));
    const auto bottom = evr::ui_layer::contentToImage(1.0f, 1.0f, band, 2064, 2100);
    CHECK(bottom.u == doctest::Approx(1.0));
    CHECK(bottom.v == doctest::Approx(1630.0 / 2100.0));
    // The whole image: unchanged.
    const auto whole = evr::ui_layer::contentToImage(0.25f, 0.75f, {0, 0, 1920, 1080}, 1920, 1080);
    CHECK(whole.u == doctest::Approx(0.25));
    CHECK(whole.v == doctest::Approx(0.75));
}

TEST_CASE("ui settings: the 16:9 crop is on by default and can be turned off") {
    std::vector<std::string> warnings;
    CHECK(readUiSettings(envOf({}), warnings).wideCrop);
    CHECK_FALSE(readUiSettings(envOf({{L"ETERNALVR_UI_CROP", L"0"}}), warnings).wideCrop);
    CHECK(warnings.empty());
}

TEST_CASE("ui settings: the wash is removed by default and can be kept") {
    std::vector<std::string> warnings;
    CHECK(readUiSettings(envOf({}), warnings).removeWash);
    CHECK_FALSE(readUiSettings(envOf({{L"ETERNALVR_UI_WASH", L"0"}}), warnings).removeWash);
    CHECK(warnings.empty());
}

TEST_CASE("ui settings: the vignette is off by default, light or strong when asked") {
    using evr::ui_layer::VignetteMode;
    std::vector<std::string> warnings;
    CHECK(readUiSettings(envOf({}), warnings).vignette == VignetteMode::Off);
    CHECK(readUiSettings(envOf({{L"ETERNALVR_VIGNETTE", L"light"}}), warnings).vignette ==
          VignetteMode::Light);
    CHECK(readUiSettings(envOf({{L"ETERNALVR_VIGNETTE", L" Strong "}}), warnings).vignette ==
          VignetteMode::Strong);
    CHECK(readUiSettings(envOf({{L"ETERNALVR_VIGNETTE", L"off"}}), warnings).vignette == VignetteMode::Off);
    CHECK(warnings.empty());
    CHECK(readUiSettings(envOf({{L"ETERNALVR_VIGNETTE", L"max"}}), warnings).vignette == VignetteMode::Off);
    CHECK(warnings.size() == 1);
}
