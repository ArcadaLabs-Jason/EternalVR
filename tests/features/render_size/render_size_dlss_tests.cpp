// Resolution's Auto with DLSS: the pixel budget holds what DLSS renders (render_size.hpp, autoSize). The
// launcher's RenderSizeDlssTests repeat these cases with the same numbers.

#include "features/render_size/render_size.hpp"

#include "features/render_size/edge_margin.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace doctest {
template <>
struct StringMaker<evr::render_size::Extent> {
    static String convert(const evr::render_size::Extent& e) {
        return (std::to_string(e.width) + "x" + std::to_string(e.height)).c_str();
    }
};
} // namespace doctest

using evr::render_size::autoSize;
using evr::render_size::describe;
using evr::render_size::dlssInputShare;
using evr::render_size::Extent;
using evr::render_size::kDefaultPixelBudget;
using evr::render_size::kDlssOutputCap;
using evr::render_size::kEdgeMarginLarge;
using evr::render_size::kEdgeMarginMedium;
using evr::render_size::kEdgeMarginSmall;
using evr::render_size::Mode;
using evr::render_size::Request;
using evr::render_size::requestGrowth;
using evr::render_size::selectSize;
using evr::render_size::ViewLimits;

namespace {

// r_dlssQuality values.
constexpr int kUltraPerformance = 0;
constexpr int kPerformance = 1;
constexpr int kBalanced = 2;
constexpr int kQuality = 3;

ViewLimits headset(std::uint32_t w, std::uint32_t h) {
    ViewLimits l;
    l.recommended = {w, h};
    l.maxImageRect = {4096, 4096};
    l.maxSwapchain = {16384, 16384};
    l.eyesSideBySide = 2;
    return l;
}

// Quest 3 through VDXR at its default (High), and at Virtual Desktop's Godlike preset.
ViewLimits vdxr() {
    return headset(2496, 2688);
}
ViewLimits godlike() {
    return headset(3072, 3264);
}
// A recommendation large enough that DLSS Quality's input passes the budget.
ViewLimits large() {
    return headset(3840, 3840);
}
// The test PC's simulator: 1280 x 1400, 2048 per eye side by side.
ViewLimits simulator() {
    ViewLimits l;
    l.recommended = {1280, 1400};
    l.maxSwapchain = {4096, 4096};
    l.eyesSideBySide = 2;
    return l;
}

std::optional<Extent> dlss(const ViewLimits& l, int quality, float scale = 1.0f, double growth = 1.0) {
    return autoSize(l, scale, kDefaultPixelBudget, growth, dlssInputShare(quality, false));
}

} // namespace

TEST_CASE("dlss input share: NVIDIA's factors per r_dlssQuality, 1 for DLAA and an unknown quality") {
    CHECK(dlssInputShare(kUltraPerformance, false) == doctest::Approx(1.0 / 3.0));
    CHECK(dlssInputShare(kPerformance, false) == doctest::Approx(0.5));
    CHECK(dlssInputShare(kBalanced, false) == doctest::Approx(0.58));
    CHECK(dlssInputShare(kQuality, false) == doctest::Approx(2.0 / 3.0));
    CHECK(dlssInputShare(kQuality, true) == 1.0); // DLAA (r_dlssQuality held at Quality)
    CHECK(dlssInputShare(-1, false) == 1.0);      // the game's own quality
    CHECK(dlssInputShare(4, false) == 1.0);
}

TEST_CASE("auto with dlss: VDXR's default renders the recommendation, DLSS's input within the budget") {
    // Today's size without DLSS (and with DLAA): 2056 x 2216.
    CHECK(*autoSize(vdxr(), 1.0f) == Extent{2056, 2216});
    CHECK(*autoSize(vdxr(), 1.0f, kDefaultPixelBudget, 1.0, 1.0) == Extent{2056, 2216});
    CHECK(*autoSize(vdxr(), 1.0f, kDefaultPixelBudget, 1.0, dlssInputShare(kQuality, true)) ==
          Extent{2056, 2216});
    // Quality's input at 2496 x 2688 is about 1664 x 1792 (2.98 Mpix): never above the recommendation.
    for (const int q : {kUltraPerformance, kPerformance, kBalanced, kQuality}) {
        CAPTURE(q);
        CHECK(*dlss(vdxr(), q) == Extent{2496, 2688});
    }
}

TEST_CASE("auto with dlss: Godlike and a larger recommendation stay within the output cap") {
    // Godlike's 3072 x 3264 (10.0 Mpix) passes 1.5 times the budget (6.84 Mpix): scaled down to it, at every
    // quality; Quality's input is then about 1691 x 1797.
    CHECK(kDlssOutputCap == 1.5);
    CHECK(*autoSize(godlike(), 1.0f) == Extent{2072, 2200});
    for (const int q : {kUltraPerformance, kPerformance, kBalanced, kQuality}) {
        CAPTURE(q);
        CHECK(*dlss(godlike(), q) == Extent{2536, 2696});
        CHECK(*dlss(large(), q) == Extent{2616, 2616});
        for (const Extent e : {*dlss(godlike(), q), *dlss(large(), q)}) {
            CHECK(static_cast<double>(e.width) * e.height <=
                  static_cast<double>(kDefaultPixelBudget) * 1.5 * 1.01);
            const double input = e.width * dlssInputShare(q, false);
            CHECK(input * input <= static_cast<double>(kDefaultPixelBudget) * 1.01);
        }
    }
    CHECK(*autoSize(large(), 1.0f) == Extent{2136, 2136});
    // DLAA keeps the budget on the output, as TAA does.
    CHECK(*autoSize(godlike(), 1.0f, kDefaultPixelBudget, 1.0, dlssInputShare(kQuality, true)) ==
          Extent{2072, 2200});
}

TEST_CASE("auto with dlss: the simulator's recommendation is within the budget either way") {
    for (const int q : {kUltraPerformance, kPerformance, kBalanced, kQuality}) {
        CAPTURE(q);
        CHECK(*dlss(simulator(), q) == Extent{1280, 1400});
        CHECK(*dlss(simulator(), q, 1.0f, static_cast<double>(kEdgeMarginMedium)) == Extent{1536, 1680});
    }
}

TEST_CASE("auto with dlss: the scale and the edge margin's growth multiply after the budget, then the fit") {
    CHECK(*dlss(vdxr(), kQuality, 0.8f) == Extent{2000, 2152});
    CHECK(*dlss(vdxr(), kQuality, 1.2f) == Extent{2992, 3224});
    // The cap holds before the scale: a scale above 1 goes past it on purpose, as past the budget with TAA.
    CHECK(*dlss(godlike(), kQuality, 0.8f) == Extent{2032, 2160});
    CHECK(*dlss(godlike(), kQuality, 1.2f) == Extent{3048, 3232});
    CHECK(*dlss(vdxr(), kQuality, 1.0f, static_cast<double>(kEdgeMarginSmall)) == Extent{2744, 2960});
    CHECK(*dlss(vdxr(), kQuality, 1.0f, static_cast<double>(kEdgeMarginMedium)) == Extent{2992, 3224});
    CHECK(*dlss(vdxr(), kQuality, 1.0f, static_cast<double>(kEdgeMarginLarge)) == Extent{3248, 3496});
    // The edge margin grows the capped size too: Godlike with Large fits the runtime's 4096.
    CHECK(*dlss(godlike(), kQuality, 1.0f, static_cast<double>(kEdgeMarginLarge)) == Extent{3296, 3504});
    Request r;
    r.mode = Mode::Auto;
    r.dlssInput = dlssInputShare(kQuality, false);
    const auto marginLarge = static_cast<double>(kEdgeMarginLarge);
    CHECK(requestGrowth(r, godlike(), marginLarge) == marginLarge);
    CHECK(requestGrowth(r, vdxr(), marginLarge) == marginLarge);
    // With 1.2 as well it passes 4096: fitted down, and only part of the margin is kept.
    CHECK(*dlss(godlike(), kQuality, 1.2f, static_cast<double>(kEdgeMarginLarge)) == Extent{3856, 4096});
    r.scale = 1.2f;
    CHECK(requestGrowth(r, godlike(), marginLarge) == doctest::Approx(1.2666032).epsilon(1e-6));
    // Without DLSS the same margin fits.
    r.dlssInput = 1.0;
    CHECK(requestGrowth(r, godlike(), marginLarge) == marginLarge);
}

TEST_CASE("auto with dlss: through the request, as the layer selects it; a bad share counts as none") {
    Request r;
    r.mode = Mode::Auto;
    r.dlssInput = dlssInputShare(kQuality, false);
    CHECK(*selectSize(r, vdxr()) == Extent{2496, 2688});
    r.growth = static_cast<double>(kEdgeMarginMedium);
    CHECK(*selectSize(r, vdxr()) == Extent{2992, 3224});
    // A fixed size is used as it is.
    Request fixed;
    fixed.mode = Mode::Fixed;
    fixed.fixed = {2056, 2216};
    fixed.dlssInput = dlssInputShare(kQuality, false);
    CHECK(*selectSize(fixed, vdxr()) == Extent{2056, 2216});
    for (const double bad : {0.0, -0.5, 1.5, std::nan("")}) {
        CAPTURE(bad);
        CHECK(*autoSize(vdxr(), 1.0f, kDefaultPixelBudget, 1.0, bad) == Extent{2056, 2216});
    }
}

TEST_CASE("describe with dlss") {
    Request a;
    a.mode = Mode::Auto;
    a.dlssInput = dlssInputShare(kQuality, false);
    CHECK(describe(a) == "auto x1.00, the budget on DLSS's input x0.667");
    a.growth = 1.2;
    CHECK(describe(a) == "auto x1.00, edge margin x1.20, the budget on DLSS's input x0.667");
    a.dlssInput = 1.0;
    CHECK(describe(a) == "auto x1.00, edge margin x1.20");
}
