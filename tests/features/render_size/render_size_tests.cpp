#include "features/render_size/render_size.hpp"

#include <doctest/doctest.h>

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
using evr::render_size::fitToLimits;
using evr::render_size::kDefaultPixelBudget;
using evr::render_size::Mode;
using evr::render_size::parseRenderScale;
using evr::render_size::parseRenderSize;
using evr::render_size::parseWindowRect;
using evr::render_size::Request;
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
    Request f;
    f.mode = Mode::Fixed;
    f.fixed = {2064, 2208};
    CHECK(describe(f) == "2064x2208");
}
