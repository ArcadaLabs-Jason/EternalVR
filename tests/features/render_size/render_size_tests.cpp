#include "features/render_size/render_size.hpp"

#include "features/render_size/edge_margin.hpp"

#include <doctest/doctest.h>

#include <cmath>
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
using evr::render_size::Extent;
using evr::render_size::fittedGrowth;
using evr::render_size::fitToLimits;
using evr::render_size::kDefaultPixelBudget;
using evr::render_size::kEdgeMarginLarge;
using evr::render_size::kEdgeMarginMedium;
using evr::render_size::kEdgeMarginSmall;
using evr::render_size::Mode;
using evr::render_size::parseRenderScale;
using evr::render_size::parseRenderSize;
using evr::render_size::parseWindowRect;
using evr::render_size::Request;
using evr::render_size::requestGrowth;
using evr::render_size::selectSize;
using evr::render_size::ViewLimits;
using evr::render_size::WindowRect;

namespace {

ViewLimits quest3Vdxr() {
    ViewLimits l;
    l.recommended = {2496, 2688};
    l.maxImageRect = {4096, 4096};
    l.maxSwapchain = {16384, 16384};
    l.eyesSideBySide = 2;
    return l;
}

} // namespace

TEST_CASE("render size setting") {
    CHECK(parseRenderSize(L"")->mode == Mode::Off);
    CHECK(parseRenderSize(L" off ")->mode == Mode::Off);
    CHECK(parseRenderSize(L"0")->mode == Mode::Off);
    CHECK(parseRenderSize(L"Auto")->mode == Mode::Auto);
    const auto fixed = parseRenderSize(L"2064x2208");
    REQUIRE(fixed);
    CHECK(fixed->mode == Mode::Fixed);
    CHECK(fixed->fixed == Extent{2064, 2208});
    CHECK(parseRenderSize(L" 1024X1024 ")->fixed == Extent{1024, 1024});
    CHECK_FALSE(parseRenderSize(L"255x1024"));  // below the minimum side
    CHECK_FALSE(parseRenderSize(L"8193x1024")); // above the maximum
    CHECK_FALSE(parseRenderSize(L"2064"));
    CHECK_FALSE(parseRenderSize(L"2064x"));
    CHECK_FALSE(parseRenderSize(L"x2208"));
    CHECK_FALSE(parseRenderSize(L"2064x2208x3"));
    CHECK_FALSE(parseRenderSize(L"-2064x2208"));
    CHECK_FALSE(parseRenderSize(L"big"));
}

TEST_CASE("render scale setting") {
    CHECK(*parseRenderScale(L"") == doctest::Approx(1.0));
    CHECK(*parseRenderScale(L"auto") == doctest::Approx(1.0));
    CHECK(*parseRenderScale(L" 0.8 ") == doctest::Approx(0.8));
    CHECK(*parseRenderScale(L"1.25") == doctest::Approx(1.25));
    CHECK(*parseRenderScale(L"2") == doctest::Approx(2.0));
    CHECK(*parseRenderScale(L"0.5") == doctest::Approx(0.5));
    CHECK_FALSE(parseRenderScale(L"0.49"));
    CHECK_FALSE(parseRenderScale(L"2.01"));
    CHECK_FALSE(parseRenderScale(L"1e0"));
    CHECK_FALSE(parseRenderScale(L"-1"));
    CHECK_FALSE(parseRenderScale(L"1.2.3"));
    CHECK_FALSE(parseRenderScale(L"."));
    CHECK_FALSE(parseRenderScale(L"nan"));
}

TEST_CASE("auto: Quest 3 through VDXR at 100 % fits the default budget, near-square") {
    const auto size = autoSize(quest3Vdxr(), 1.0f);
    REQUIRE(size);
    CHECK(*size == Extent{2056, 2216});
    CHECK(static_cast<double>(size->width) * size->height <= static_cast<double>(kDefaultPixelBudget) * 1.01);
    // The recommendation's aspect is kept.
    CHECK(static_cast<double>(size->width) / size->height == doctest::Approx(2496.0 / 2688.0).epsilon(0.01));
}

TEST_CASE("auto in mono: the one image gets one stereo eye's budget, and the swapchain's width to itself") {
    // Head-tracked mono's ring holds one image (eyesSideBySide 1): the same cases as the launcher's
    // RenderSizeMonoTests. Within the limits mono's size is one stereo eye's.
    for (const ViewLimits& stereo :
         {quest3Vdxr(), [] {
              ViewLimits godlike = quest3Vdxr();
              godlike.recommended = {3072, 3264}; // Virtual Desktop's Godlike on a Quest 3
              return godlike;
          }()}) {
        ViewLimits mono = stereo;
        mono.eyesSideBySide = 1;
        for (const float scale : {0.5f, 1.0f, 1.5f, 2.0f}) {
            CHECK(*autoSize(mono, scale) == *autoSize(stereo, scale));
        }
    }
    ViewLimits vdxr = quest3Vdxr();
    vdxr.eyesSideBySide = 1;
    CHECK(*autoSize(vdxr, 1.0f) == Extent{2056, 2216});
    vdxr.recommended = {3072, 3264};
    CHECK(*autoSize(vdxr, 1.0f) == Extent{2072, 2200});
    CHECK(*autoSize(vdxr, 1.5f) == Extent{3104, 3304});
    // The OpenXR Simulator (4096 x 4096 swapchains): two eyes share the width, one image does not.
    ViewLimits sim;
    sim.recommended = {1280, 1400};
    sim.maxImageRect = {4096, 4096};
    sim.maxSwapchain = {4096, 4096};
    sim.eyesSideBySide = 2;
    CHECK(*autoSize(sim, 1.0f) == Extent{1280, 1400});
    CHECK(*autoSize(sim, 2.0f) == Extent{2048, 2240});
    sim.eyesSideBySide = 1;
    CHECK(*autoSize(sim, 1.0f) == Extent{1280, 1400});
    CHECK(*autoSize(sim, 2.0f) == Extent{2560, 2800});
    CHECK(fitToLimits({2496, 2688}, sim) == Extent{2496, 2688});
}

TEST_CASE("auto: a recommendation within the budget is used as it is, never scaled up") {
    ViewLimits l;
    l.recommended = {1440, 1584};
    CHECK(*autoSize(l, 1.0f) == Extent{1440, 1584});
    // The owner's Virtual Desktop at "57 %" recommends about 1880 x 2024.
    l.recommended = {1880, 2024};
    CHECK(*autoSize(l, 1.0f) == Extent{1880, 2024});
}

TEST_CASE("auto: the render scale multiplies the budgeted size, past the budget when above 1") {
    const auto half = autoSize(quest3Vdxr(), 0.5f);
    REQUIRE(half);
    CHECK(*half == Extent{1032, 1112});
    const auto more = autoSize(quest3Vdxr(), 1.2f);
    REQUIRE(more);
    CHECK(*more == Extent{2472, 2656});
    CHECK(static_cast<std::uint64_t>(more->width) * more->height > kDefaultPixelBudget);
}

TEST_CASE("auto: the edge margin's growth multiplies both sides after the scale, past the budget") {
    // The launcher's sizes (edge_margin.hpp) on Quest 3 through VDXR at 100 %: 2056 x 2216 without.
    CHECK(*autoSize(quest3Vdxr(), 1.0f, kDefaultPixelBudget, 1.0) == Extent{2056, 2216});
    CHECK(*autoSize(quest3Vdxr(), 1.0f, kDefaultPixelBudget, static_cast<double>(kEdgeMarginSmall)) ==
          Extent{2264, 2440});
    CHECK(*autoSize(quest3Vdxr(), 1.0f, kDefaultPixelBudget, static_cast<double>(kEdgeMarginMedium)) ==
          Extent{2472, 2656});
    CHECK(*autoSize(quest3Vdxr(), 1.0f, kDefaultPixelBudget, static_cast<double>(kEdgeMarginLarge)) ==
          Extent{2672, 2880});
    // The test PC's simulator (1280 x 1400, 2048 per eye side by side).
    ViewLimits sim;
    sim.recommended = {1280, 1400};
    sim.maxSwapchain = {4096, 4096};
    sim.eyesSideBySide = 2;
    CHECK(*autoSize(sim, 1.0f, kDefaultPixelBudget, static_cast<double>(kEdgeMarginMedium)) ==
          Extent{1536, 1680});
    // Through the request, as the layer selects it.
    Request r;
    r.mode = Mode::Auto;
    r.growth = static_cast<double>(kEdgeMarginMedium);
    CHECK(*selectSize(r, quest3Vdxr()) == Extent{2472, 2656});
    // A bad growth gives nothing, as a bad scale does.
    CHECK_FALSE(autoSize(quest3Vdxr(), 1.0f, kDefaultPixelBudget, 0.0));
    CHECK_FALSE(autoSize(quest3Vdxr(), 1.0f, kDefaultPixelBudget, -1.0));
}

TEST_CASE("auto: sides are multiples of 8") {
    ViewLimits l;
    l.recommended = {1833, 1917};
    const auto size = autoSize(l, 1.0f);
    REQUIRE(size);
    CHECK(size->width % 8 == 0);
    CHECK(size->height % 8 == 0);
    CHECK(*size == Extent{1832, 1920});
}

TEST_CASE("auto: nothing without a recommendation or with a bad scale") {
    ViewLimits l;
    CHECK_FALSE(autoSize(l, 1.0f));
    l.recommended = {2000, 2000};
    CHECK_FALSE(autoSize(l, 0.0f));
    CHECK_FALSE(autoSize(l, -1.0f));
}

TEST_CASE("fit: the runtime's limits scale the size down uniformly") {
    ViewLimits l;
    l.maxImageRect = {2048, 4096};
    const Extent fitted = fitToLimits({4096, 4096}, l);
    CHECK(fitted == Extent{2048, 2048});
    // Two eyes side by side share the swapchain's width.
    ViewLimits ring;
    ring.maxSwapchain = {4096, 4096};
    ring.eyesSideBySide = 2;
    CHECK(fitToLimits({2496, 2688}, ring) == Extent{2048, 2208});
    // No limits: only Vulkan's usual maximum.
    CHECK(fitToLimits({20000, 10000}, ViewLimits{}) == Extent{16384, 8192});
    CHECK(fitToLimits({0, 100}, ViewLimits{}) == Extent{});
}

TEST_CASE("fit: tiny sizes are raised to the minimum side") {
    CHECK(fitToLimits({100, 120}, ViewLimits{}) == Extent{256, 256});
}

TEST_CASE("select: off, fixed with and without limits, auto only with limits") {
    Request off;
    CHECK_FALSE(selectSize(off, quest3Vdxr()));

    Request fixed;
    fixed.mode = Mode::Fixed;
    fixed.fixed = {2064, 2208};
    CHECK(*selectSize(fixed, std::nullopt) == Extent{2064, 2208});
    ViewLimits small;
    small.maxImageRect = {1032, 4096};
    CHECK(*selectSize(fixed, small) == Extent{1032, 1104});

    Request automatic;
    automatic.mode = Mode::Auto;
    CHECK_FALSE(selectSize(automatic, std::nullopt));
    CHECK(*selectSize(automatic, quest3Vdxr()) == Extent{2056, 2216});
    automatic.scale = 0.8f;
    CHECK(*selectSize(automatic, quest3Vdxr()) == Extent{1648, 1776});
}

TEST_CASE("edge margin: the margin a fitted size keeps") {
    const auto medium = static_cast<double>(kEdgeMarginMedium);
    const auto small = static_cast<double>(kEdgeMarginSmall);
    Request automatic;
    automatic.mode = Mode::Auto;
    SUBCASE("the grown size fits: the whole margin") {
        CHECK(requestGrowth(automatic, quest3Vdxr(), medium) == medium);
        ViewLimits sim;
        sim.recommended = {1280, 1400};
        sim.maxSwapchain = {4096, 4096};
        sim.eyesSideBySide = 2;
        CHECK(requestGrowth(automatic, sim, medium) == medium);
    }
    SUBCASE("clamped: the margin shrinks to what the fitted size holds at the centre's density") {
        // Quest 3's 2496 x 2688 in the budget is 2057 x 2215; Medium wants 2469 x 2658. An image limit of
        // 2300 holds 2300 / 2469 of it, x1.118 of the view at the ungrown density.
        ViewLimits narrow = quest3Vdxr();
        narrow.maxImageRect = {2300, 4096};
        CHECK(requestGrowth(automatic, narrow, medium) == doctest::Approx(1.1178615).epsilon(1e-6));
        // Small's 2263 wide still fits: all of it.
        CHECK(requestGrowth(automatic, narrow, small) == small);
        // The simulator's 2048 per eye cuts the ungrown size too, by the same share: no margin is left.
        ViewLimits sim = quest3Vdxr();
        sim.maxImageRect = {};
        sim.maxSwapchain = {4096, 4096};
        CHECK(requestGrowth(automatic, sim, medium) == doctest::Approx(1.0));
    }
    SUBCASE("a fixed size is taken as already grown (the launcher's)") {
        Request fixed;
        fixed.mode = Mode::Fixed;
        fixed.fixed = {2472, 2656};
        CHECK(requestGrowth(fixed, quest3Vdxr(), medium) == medium);
        ViewLimits narrow = quest3Vdxr();
        narrow.maxImageRect = {2300, 4096};
        CHECK(requestGrowth(fixed, narrow, medium) == doctest::Approx(1.1165049).epsilon(1e-6));
        // Never below 1, even when the limits cut the ungrown size more than the grown one.
        narrow.maxImageRect = {2000, 4096};
        CHECK(requestGrowth(fixed, narrow, medium) == doctest::Approx(1.0));
        CHECK(fittedGrowth({2472, 2656}, medium, narrow) >= 1.0);
    }
    SUBCASE("off keeps the margin; no margin or a bad one is 1") {
        CHECK(requestGrowth(Request{}, quest3Vdxr(), medium) == medium);
        CHECK(requestGrowth(automatic, quest3Vdxr(), 1.0) == 1.0);
        CHECK(requestGrowth(automatic, quest3Vdxr(), 0.5) == 1.0);
        CHECK(requestGrowth(automatic, quest3Vdxr(), std::nan("")) == 1.0);
        CHECK(fittedGrowth({}, medium, quest3Vdxr()) == medium);
    }
}

TEST_CASE("window rectangles") {
    CHECK(*parseWindowRect(L"2560,0,2064,2100") == WindowRect{2560, 0, 2064, 2100});
    CHECK(*parseWindowRect(L" -1920, 10 ,1280,720 ") == WindowRect{-1920, 10, 1280, 720});
    CHECK_FALSE(parseWindowRect(L"0,0,1280"));
    CHECK_FALSE(parseWindowRect(L"0,0,1280,720,1"));
    CHECK_FALSE(parseWindowRect(L"0,0,0,720"));
    CHECK_FALSE(parseWindowRect(L"0,0,1280,-720"));
    CHECK_FALSE(parseWindowRect(L"a,0,1280,720"));
    CHECK_FALSE(parseWindowRect(L""));
}

TEST_CASE("describe") {
    CHECK(describe(Request{}) == "off");
    Request a;
    a.mode = Mode::Auto;
    a.scale = 0.8f;
    CHECK(describe(a) == "auto x0.80");
    a.growth = 1.2;
    CHECK(describe(a) == "auto x0.80, edge margin x1.20");
    Request f;
    f.mode = Mode::Fixed;
    f.fixed = {2064, 2208};
    CHECK(describe(f) == "2064x2208");
}
