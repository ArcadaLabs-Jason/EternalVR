#include "features/render_size/render_cap.hpp"

#include <doctest/doctest.h>

#include <optional>
#include <string>

using evr::render_size::capped;
using evr::render_size::cappedPercent;
using evr::render_size::describeCap;
using evr::render_size::Extent;
using evr::render_size::FrameInsets;
using evr::render_size::largestEyeWindow;
using evr::render_size::percentOf;
using evr::render_size::WindowRect;

namespace {

// A resizable window with a title bar on Windows 11 at 100 %: 8 px invisible borders, a 31 px caption.
constexpr FrameInsets kFrame{8, 31, 8, 8};

} // namespace

TEST_CASE("cap: percentages round down, per side") {
    CHECK(percentOf(958, 2016) == 47);
    CHECK(percentOf(1009, 2112) == 47);
    CHECK(percentOf(672, 1344) == 50);
    CHECK(percentOf(2016, 2016) == 100);
    CHECK(percentOf(5, 0) == 100);
    CHECK(cappedPercent(Extent{672, 701}, Extent{1344, 1440}) == 48);
}

TEST_CASE("cap: capped when either side is below the plan") {
    CHECK(capped(Extent{958, 1009}, Extent{2016, 2112}));
    CHECK(capped(Extent{2016, 2000}, Extent{2016, 2112}));
    CHECK_FALSE(capped(Extent{2016, 2112}, Extent{2016, 2112}));
    CHECK_FALSE(capped(Extent{2100, 2200}, Extent{2016, 2112}));
    CHECK_FALSE(capped(Extent{958, 1009}, Extent{0, 0}));
}

TEST_CASE("cap: the log text") {
    CHECK(describeCap(Extent{958, 1009}, Extent{2016, 2112}) == "958x1009, 47% of the planned 2016x2112");
    CHECK(describeCap(Extent{672, 701}, Extent{1344, 1440}) == "672x701, 50% x 48% of the planned 1344x1440");
}

TEST_CASE("cap: the largest eye-shaped window inside a work area") {
    // The rig's virtual display: 1280x800, no taskbar. Room for 1264x761; the eye (1344x1440) is limited by
    // the height: 761 * 1344 / 1440 = 710.27, rounded to 710.
    const auto rig = largestEyeWindow(Extent{1344, 1440}, WindowRect{0, 0, 1280, 800}, kFrame);
    REQUIRE(rig.has_value());
    CHECK(*rig == WindowRect{8 + (1264 - 710) / 2, 31, 710, 761});
    // Its framed window fits the work area.
    CHECK(rig->x - kFrame.left >= 0);
    CHECK(rig->x + rig->width + kFrame.right <= 1280);
    CHECK(rig->y + rig->height + kFrame.bottom <= 800);

    // A 2560x1440 monitor with a 48 px taskbar: 1353 high; a 2016x2112 eye: 1353 * 2016 / 2112 = 1291.5,
    // rounded to 1292.
    const auto desk = largestEyeWindow(Extent{2016, 2112}, WindowRect{0, 0, 2560, 1392}, kFrame);
    REQUIRE(desk.has_value());
    CHECK(desk->height == 1353);
    CHECK(desk->width == 1292);
    CHECK(desk->y == 31);

    // On a display to the right: the same window, moved.
    const auto right = largestEyeWindow(Extent{1344, 1440}, WindowRect{2560, 0, 1280, 800}, kFrame);
    REQUIRE(right.has_value());
    CHECK(right->x == rig->x + 2560);
}

TEST_CASE("cap: never larger than the eye") {
    const auto big = largestEyeWindow(Extent{1000, 1000}, WindowRect{0, 0, 3840, 2160}, kFrame);
    REQUIRE(big.has_value());
    CHECK(big->width == 1000);
    CHECK(big->height == 1000);
    CHECK(big->x == 8 + (3824 - 1000) / 2);
    CHECK(big->y == 31 + (2121 - 1000) / 2);
}

TEST_CASE("cap: nothing for an empty eye or a work area smaller than the frame") {
    CHECK_FALSE(largestEyeWindow(Extent{0, 0}, WindowRect{0, 0, 1280, 800}, kFrame).has_value());
    CHECK_FALSE(largestEyeWindow(Extent{1344, 1440}, WindowRect{0, 0, 16, 30}, kFrame).has_value());
}
