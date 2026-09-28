#include "features/render_size/mirror_window.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace doctest {
template <>
struct StringMaker<evr::render_size::WindowRect> {
    static String convert(const evr::render_size::WindowRect& r) {
        return (std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.width) + "x" +
                std::to_string(r.height))
            .c_str();
    }
};
template <>
struct StringMaker<evr::render_size::Band> {
    static String convert(const evr::render_size::Band& b) {
        return ("rows " + std::to_string(b.y) + "+" + std::to_string(b.height)).c_str();
    }
};
} // namespace doctest

using evr::render_size::Band;
using evr::render_size::centredBand;
using evr::render_size::DisplayChoice;
using evr::render_size::Extent;
using evr::render_size::keepFrameOnScreen;
using evr::render_size::MirrorDisplay;
using evr::render_size::MirrorSize;
using evr::render_size::Monitor;
using evr::render_size::parseAspect;
using evr::render_size::parseMirrorDisplay;
using evr::render_size::parseMirrorSize;
using evr::render_size::placeMirror;
using evr::render_size::shapeToImage;
using evr::render_size::WindowRect;

namespace {

bool near(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}

// The owner's rig: a 2560x1440 primary monitor with a taskbar, and a headset app's 3840x2160 virtual display
// to its right (where the launcher puts the mirror).
std::vector<Monitor> rig() {
    return {Monitor{{0, 0, 2560, 1440}, {0, 0, 2560, 1392}, true},
            Monitor{{2560, 0, 3840, 2160}, {2560, 0, 3840, 2160}, false}};
}

constexpr WindowRect kLauncher{2560, 0, 1280, 720};

} // namespace

TEST_CASE("aspect setting") {
    CHECK(near(*parseAspect(L"16:9"), 16.0 / 9.0));
    CHECK(near(*parseAspect(L" 16:10 "), 1.6));
    CHECK(near(*parseAspect(L"21:9"), 21.0 / 9.0));
    CHECK(near(*parseAspect(L"full"), 0.0));
    CHECK(near(*parseAspect(L"OFF"), 0.0));
    CHECK(near(*parseAspect(L"0"), 0.0));
    CHECK_FALSE(parseAspect(L"").has_value());
    CHECK_FALSE(parseAspect(L"wide").has_value());
    CHECK_FALSE(parseAspect(L"9:16").has_value()); // taller than square
    CHECK_FALSE(parseAspect(L"16:0").has_value());
    CHECK_FALSE(parseAspect(L"16:").has_value());
}

TEST_CASE("band: the centred 16:9 rows of a tall eye image") {
    // 2056 / (16/9) = 1156.5, rounded to 1157; (2216 - 1157) / 2 = 529 rows above.
    CHECK(centredBand(2056, 2216, 16.0 / 9.0) == Band{529, 1157});
    CHECK(centredBand(2056, 2216, 1.6) == Band{465, 1285});
    CHECK(centredBand(2064, 2100, 16.0 / 9.0) == Band{469, 1161});
}

TEST_CASE("band: the whole image when there is nothing to cut") {
    CHECK(centredBand(2056, 2216, 0.0) == Band{0, 2216});
    CHECK(centredBand(3840, 2160, 16.0 / 9.0) == Band{0, 2160});
    CHECK(centredBand(1920, 1085, 16.0 / 9.0) == Band{0, 1085}); // within the 0.5 % of rounding
    CHECK(centredBand(3840, 1600, 16.0 / 9.0) == Band{0, 1600}); // wider than the band: not cut sideways
    CHECK(centredBand(0, 2216, 16.0 / 9.0) == Band{0, 2216});
}

TEST_CASE("mirror display setting") {
    CHECK(parseMirrorDisplay(L"")->choice == DisplayChoice::Launcher);
    CHECK(parseMirrorDisplay(L"Launcher")->choice == DisplayChoice::Launcher);
    CHECK(parseMirrorDisplay(L"primary")->choice == DisplayChoice::Primary);
    const auto second = parseMirrorDisplay(L" 2 ");
    REQUIRE(second.has_value());
    CHECK(second->choice == DisplayChoice::Number);
    CHECK(second->number == 2);
    const auto point = parseMirrorDisplay(L"-1920,300");
    REQUIRE(point.has_value());
    CHECK(point->choice == DisplayChoice::Point);
    CHECK(point->x == -1920);
    CHECK(point->y == 300);
    CHECK_FALSE(parseMirrorDisplay(L"0").has_value());
    CHECK_FALSE(parseMirrorDisplay(L"left").has_value());
    CHECK_FALSE(parseMirrorDisplay(L"1.5").has_value());
    CHECK_FALSE(parseMirrorDisplay(L"10,").has_value());
}

TEST_CASE("mirror size setting") {
    const auto size = parseMirrorSize(L"1920x1080");
    REQUIRE(size.has_value());
    CHECK(size->size.width == 1920u);
    CHECK(size->size.height == 1080u);
    CHECK(size->scale == 0.0f);
    const auto scale = parseMirrorSize(L"1.5");
    REQUIRE(scale.has_value());
    CHECK(scale->scale == 1.5f);
    CHECK(parseMirrorSize(L"640X360").has_value());
    CHECK_FALSE(parseMirrorSize(L"").has_value());
    CHECK_FALSE(parseMirrorSize(L"0.1").has_value());
    CHECK_FALSE(parseMirrorSize(L"5").has_value());
    CHECK_FALSE(parseMirrorSize(L"32x32").has_value());
    CHECK_FALSE(parseMirrorSize(L"1920x").has_value());
    CHECK_FALSE(parseMirrorSize(L"big").has_value());
    const auto fill = parseMirrorSize(L" Fill ");
    REQUIRE(fill.has_value());
    CHECK(fill->fill);
    CHECK(fill->scale == 0.0f);
    CHECK_FALSE(parseMirrorSize(L"1.5")->fill);
    CHECK_FALSE(parseMirrorSize(L"filled").has_value());
}

TEST_CASE("mirror placement: nothing asked keeps the launcher's rectangle") {
    const auto p = placeMirror(kLauncher, MirrorDisplay{}, std::nullopt, 0.0, rig());
    CHECK(p.rect == kLauncher);
    CHECK(p.displayFound);
}

TEST_CASE("mirror placement: the primary monitor, centred in its work area") {
    const MirrorDisplay primary{DisplayChoice::Primary, 0, 0, 0};
    const auto p = placeMirror(kLauncher, primary, std::nullopt, 0.0, rig());
    CHECK(p.rect == WindowRect{640, 336, 1280, 720});
}

TEST_CASE("mirror placement: a display by number, or by a point on it") {
    const auto second = placeMirror(kLauncher, MirrorDisplay{DisplayChoice::Number, 2, 0, 0},
                                    MirrorSize{{1920, 1080}, 0.0f}, 0.0, rig());
    CHECK(second.rect == WindowRect{2560 + 960, 540, 1920, 1080});
    const auto point =
        placeMirror(kLauncher, MirrorDisplay{DisplayChoice::Point, 0, 100, 100}, std::nullopt, 0.0, rig());
    CHECK(point.rect == WindowRect{640, 336, 1280, 720});
}

TEST_CASE("mirror placement: a missing display leaves the launcher's place") {
    const auto p =
        placeMirror(kLauncher, MirrorDisplay{DisplayChoice::Number, 3, 0, 0}, std::nullopt, 0.0, rig());
    CHECK_FALSE(p.displayFound);
    CHECK(p.rect == kLauncher);
    const auto off =
        placeMirror(kLauncher, MirrorDisplay{DisplayChoice::Point, 0, -5000, 0}, std::nullopt, 0.0, rig());
    CHECK_FALSE(off.displayFound);
}

TEST_CASE("mirror placement: a scale of the launcher's size, fitted to the display") {
    const auto p = placeMirror(kLauncher, MirrorDisplay{}, MirrorSize{{}, 1.5f}, 0.0, rig());
    CHECK(p.rect == WindowRect{2560, 0, 1920, 1080});
    // Too big for the primary's work area (2560x1392): scaled down uniformly, aspect kept.
    const auto big = placeMirror(kLauncher, MirrorDisplay{DisplayChoice::Primary, 0, 0, 0},
                                 MirrorSize{{3840, 2160}, 0.0f}, 0.0, rig());
    CHECK(big.rect.height == 1392);
    CHECK(big.rect.width == 2474);
    CHECK(big.rect.y == 0);
}

TEST_CASE("mirror placement: a crop makes the window that aspect") {
    // 16:9 in the launcher's 16:9 window: unchanged.
    const auto wide = placeMirror(kLauncher, MirrorDisplay{}, std::nullopt, 16.0 / 9.0, rig());
    CHECK(wide.rect == kLauncher);
    // 16:10 inside 1280x720: 1152x720.
    const auto tenth = placeMirror(kLauncher, MirrorDisplay{}, std::nullopt, 1.6, rig());
    CHECK(tenth.rect == WindowRect{2560, 0, 1152, 720});
    // A square size asked for with a 16:9 crop: the widest 16:9 inside it.
    const auto square =
        placeMirror(kLauncher, MirrorDisplay{}, MirrorSize{{1000, 1000}, 0.0f}, 16.0 / 9.0, rig());
    CHECK(square.rect == WindowRect{2560, 0, 1000, 563});
}

TEST_CASE("mirror placement: fill covers the whole display, taskbar included") {
    const MirrorSize fill{{}, 0.0f, true};
    // The primary's whole area, not its work area (2560x1392 without the taskbar).
    const auto primary =
        placeMirror(kLauncher, MirrorDisplay{DisplayChoice::Primary, 0, 0, 0}, fill, 0.0, rig());
    CHECK(primary.rect == WindowRect{0, 0, 2560, 1440});
    CHECK(primary.fills);
    CHECK(primary.display == 1);
    CHECK(primary.displayFound);
    // The rig's virtual display, by the point the launcher uses and by number.
    const auto point =
        placeMirror(kLauncher, MirrorDisplay{DisplayChoice::Point, 0, 2560, 0}, fill, 0.0, rig());
    CHECK(point.rect == WindowRect{2560, 0, 3840, 2160});
    CHECK(point.display == 2);
    const auto second =
        placeMirror(kLauncher, MirrorDisplay{DisplayChoice::Number, 2, 0, 0}, fill, 0.0, rig());
    CHECK(second.rect == WindowRect{2560, 0, 3840, 2160});
    // `launcher`: the display holding the launcher's corner.
    const auto own = placeMirror(kLauncher, MirrorDisplay{}, fill, 0.0, rig());
    CHECK(own.rect == WindowRect{2560, 0, 3840, 2160});
    CHECK(own.display == 2);
}

TEST_CASE("mirror placement: fill ignores the crop's shape (the band is stretched into the display)") {
    const MirrorSize fill{{}, 0.0f, true};
    const auto wide = placeMirror(kLauncher, MirrorDisplay{}, fill, 16.0 / 9.0, rig());
    CHECK(wide.rect == WindowRect{2560, 0, 3840, 2160});
    const auto tenth =
        placeMirror(kLauncher, MirrorDisplay{DisplayChoice::Primary, 0, 0, 0}, fill, 1.6, rig());
    CHECK(tenth.rect == WindowRect{0, 0, 2560, 1440});
    CHECK(tenth.fills);
}

TEST_CASE("mirror placement: fill on a missing display keeps the launcher's rectangle") {
    const MirrorSize fill{{}, 0.0f, true};
    const auto missing =
        placeMirror(kLauncher, MirrorDisplay{DisplayChoice::Number, 3, 0, 0}, fill, 0.0, rig());
    CHECK_FALSE(missing.displayFound);
    CHECK_FALSE(missing.fills);
    CHECK(missing.rect == kLauncher);
    const WindowRect offscreen{-5000, 0, 1280, 720};
    const auto lost = placeMirror(offscreen, MirrorDisplay{}, fill, 0.0, rig());
    CHECK_FALSE(lost.fills);
    CHECK(lost.rect == offscreen);
}

TEST_CASE("mirror placement: fill on a display left of and above the primary") {
    const std::vector<Monitor> monitors{Monitor{{0, 0, 1920, 1080}, {0, 0, 1920, 1040}, true},
                                        Monitor{{-1680, -300, 1680, 1050}, {-1680, -300, 1680, 1010}, false}};
    const auto p = placeMirror(kLauncher, MirrorDisplay{DisplayChoice::Point, 0, -10, -10},
                               MirrorSize{{}, 0.0f, true}, 0.0, monitors);
    CHECK(p.rect == WindowRect{-1680, -300, 1680, 1050});
    CHECK(p.display == 2);
}

TEST_CASE("mirror shape: a tall eye image makes the window narrower, same height") {
    // The simulator's 1280x1400 eye in the launcher's 1280x720: 658x720 at the launcher's corner.
    CHECK(shapeToImage(kLauncher, Extent{1280, 1400}, false) == WindowRect{2560, 0, 658, 720});
    // A Quest 3 eye of 2056x2216: 668x720; centred where a display choice centred the window.
    CHECK(shapeToImage(WindowRect{640, 336, 1280, 720}, Extent{2056, 2216}, true) ==
          WindowRect{946, 336, 668, 720});
}

TEST_CASE("mirror shape: a wide image makes the window lower, same width") {
    CHECK(shapeToImage(WindowRect{0, 0, 1000, 1000}, Extent{1920, 1080}, false) ==
          WindowRect{0, 0, 1000, 563});
    CHECK(shapeToImage(WindowRect{0, 0, 1000, 1000}, Extent{1920, 1080}, true) ==
          WindowRect{0, 218, 1000, 563});
}

TEST_CASE("mirror shape: an image of the window's own shape, or none, keeps the window") {
    CHECK(shapeToImage(kLauncher, Extent{1920, 1080}, true) == kLauncher);
    CHECK(shapeToImage(kLauncher, Extent{}, false) == kLauncher);
    CHECK(shapeToImage(kLauncher, Extent{0, 1400}, true) == kLauncher);
    const WindowRect empty{10, 10, 0, 0};
    CHECK(shapeToImage(empty, Extent{1280, 1400}, true) == empty);
}

TEST_CASE("mirror shape: an extreme image keeps at least one pixel per side") {
    CHECK(shapeToImage(WindowRect{0, 0, 64, 64}, Extent{16384, 1}, false) == WindowRect{0, 0, 64, 1});
    CHECK(shapeToImage(WindowRect{0, 0, 64, 64}, Extent{1, 16384}, false) == WindowRect{0, 0, 1, 64});
}

TEST_CASE("mirror shape: a placement on a display is marked centred, the launcher's place is not") {
    CHECK(placeMirror(kLauncher, MirrorDisplay{DisplayChoice::Primary, 0, 0, 0}, std::nullopt, 0.0, rig())
              .centred);
    CHECK_FALSE(placeMirror(kLauncher, MirrorDisplay{}, MirrorSize{{1280, 720}, 0.0f}, 0.0, rig()).centred);
    CHECK_FALSE(
        placeMirror(kLauncher, MirrorDisplay{DisplayChoice::Number, 3, 0, 0}, std::nullopt, 0.0, rig())
            .centred);
}

TEST_CASE("mirror window: the frame stays on the screen") {
    const WindowRect work{0, 0, 2560, 1380};
    // A client area at the display's corner: the frame (8 px sides, 31 px title bar) moves in.
    CHECK(keepFrameOnScreen(WindowRect{-8, -31, 684, 759}, work) == WindowRect{0, 0, 684, 759});
    // Already on the screen: unchanged.
    CHECK(keepFrameOnScreen(WindowRect{100, 50, 684, 759}, work) == WindowRect{100, 50, 684, 759});
    // A second display to the right: only its own corner counts.
    CHECK(keepFrameOnScreen(WindowRect{3832, -31, 684, 759}, WindowRect{3840, 0, 2560, 1380}) ==
          WindowRect{3840, 0, 684, 759});
}
