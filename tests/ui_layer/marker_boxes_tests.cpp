#include "ui_layer/marker_boxes.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using evr::ui_layer::kMarkerNearestMetres;
using evr::ui_layer::markerBoxes;
using evr::ui_layer::MarkerCopy;
using evr::ui_layer::MarkerEye;
using evr::ui_layer::markerEyeCopies;
using evr::ui_layer::MarkerParallax;
using evr::ui_layer::markerShiftPixels;
using evr::ui_layer::MarkerSpot;
using evr::ui_layer::PixelRect;

namespace {

// The 1280 x 720 band of a 1280 x 1400 GUI target (the render size `auto` of the rig).
constexpr PixelRect kBand{0, 340, 1280, 720};

// The rig's HUD quad: 2 m wide at 1.5 m, its 1280 px band across it.
constexpr MarkerParallax kQuad{1.5f, 640.0f, 1.0f};
// The eyes of a 64 mm IPD, rendered where they are shown from.
constexpr MarkerEye kLeft{-0.032f, -0.032f};
constexpr MarkerEye kRight{0.032f, 0.032f};

bool contains(const PixelRect& r, std::int64_t x, std::int64_t y) {
    return x >= r.x && y >= r.y && x < r.x + static_cast<std::int64_t>(r.width) &&
           y < r.y + static_cast<std::int64_t>(r.height);
}

} // namespace

TEST_CASE("a marker's box holds its diamond and the distance under it") {
    // A marker the rig's GUI capture shows at (739, 410) of the band, "132.2m" drawn under it to y 442.
    const std::vector<PixelRect> boxes = markerBoxes({{739.0f / 1280.0f, 410.0f / 720.0f}}, kBand);
    REQUIRE(boxes.size() == 1);
    const PixelRect& b = boxes[0];
    CHECK(contains(b, 739 - 21, 340 + 410)); // the distance's left end
    CHECK(contains(b, 739 + 21, 340 + 410));
    CHECK(contains(b, 739, 340 + 410 - 14)); // the diamond's top
    CHECK(contains(b, 739, 340 + 442 - 1));  // the distance's bottom
    CHECK_FALSE(contains(b, 739, 340 + 410 - 30));
    CHECK_FALSE(contains(b, 739 + 40, 340 + 410));
}

TEST_CASE("boxes follow the band: a bigger target gives bigger boxes in its pixels") {
    const PixelRect wide{0, 548, 2056, 1157}; // the 2056 x 2216 target of a Quest 3 at its render size
    const std::vector<PixelRect> small = markerBoxes({{0.5f, 0.5f}}, kBand);
    const std::vector<PixelRect> big = markerBoxes({{0.5f, 0.5f}}, wide);
    REQUIRE(small.size() == 1);
    REQUIRE(big.size() == 1);
    CHECK(big[0].width > small[0].width);
    CHECK(big[0].height > small[0].height);
    CHECK(contains(big[0], 1028, 548 + 578));
}

TEST_CASE("a box stops at the band's edge, and a point outside the band has none") {
    const std::vector<PixelRect> boxes = markerBoxes({{0.0f, 0.0f}, {1.2f, 0.5f}, {0.5f, -0.1f}}, kBand);
    REQUIRE(boxes.size() == 1);
    CHECK(boxes[0].x == 0);
    CHECK(boxes[0].y == 340);
    CHECK(boxes[0].x + static_cast<std::int64_t>(boxes[0].width) <= 1280);
}

TEST_CASE("overlapping boxes merge into the rectangle around them") {
    // Three frames of one marker moving right, and a marker of its own far away.
    const float y = 0.5f;
    const std::vector<PixelRect> boxes = markerBoxes(
        {{600.0f / 1280.0f, y}, {620.0f / 1280.0f, y}, {675.0f / 1280.0f, y}, {0.1f, 0.1f}}, kBand);
    REQUIRE(boxes.size() == 2);
    const PixelRect& merged = boxes[0].width > boxes[1].width ? boxes[0] : boxes[1];
    CHECK(contains(merged, 600, 340 + 360));
    CHECK(contains(merged, 675, 340 + 360));
    CHECK(contains(merged, 640, 340 + 360 - 20)); // the gap between the diamonds is inside too
}

TEST_CASE("no spots, or no band, give no boxes") {
    CHECK(markerBoxes({}, kBand).empty());
    CHECK(markerBoxes({{0.5f, 0.5f}}, PixelRect{0, 0, 0, 0}).empty());
}

TEST_CASE("a far marker moves apart by up to the eyes' distance, one at the quad's distance stays") {
    // 131 m ahead (the rig's objective marker): each eye 0.032 * (1 - 1.5 / 131) m = 20.2 px out.
    CHECK(markerShiftPixels(kQuad, kRight, 131.0f) == doctest::Approx(20.25).epsilon(0.01));
    CHECK(markerShiftPixels(kQuad, kLeft, 131.0f) == doctest::Approx(-20.25).epsilon(0.01));
    CHECK(markerShiftPixels(kQuad, kRight, 1.5f) == doctest::Approx(0.0f));
    // Nearer than the quad: the other way (crossed), and no nearer than kMarkerNearestMetres.
    CHECK(markerShiftPixels(kQuad, kRight, 0.75f) == doctest::Approx(-20.48).epsilon(0.01));
    CHECK(markerShiftPixels(kQuad, kRight, 0.01f) == markerShiftPixels(kQuad, kRight, kMarkerNearestMetres));
}

TEST_CASE(
    "an eye rendered at another separation than it is shown from moves its markers to match the world") {
    // Shown 32 mm right, rendered 35 mm right (ETERNALVR_IPD 70 mm): a target 3 m ahead.
    const MarkerEye eye{0.032f, 0.035f};
    CHECK(markerShiftPixels(kQuad, eye, 3.0f) ==
          doctest::Approx((0.032 - 0.035 * 0.5) * 640.0).epsilon(0.001));
    // A mono frame (both eyes shown and rendered from the head's centre): nothing moves.
    CHECK(markerShiftPixels(kQuad, MarkerEye{}, 40.0f) == 0.0f);
}

TEST_CASE("at a world scale the distance is taken in metres of the eyes' distance") {
    // Two units per metre: a target 262 units ahead is 131 m away for the eyes.
    const MarkerParallax scaled{1.5f, 640.0f, 2.0f};
    CHECK(markerShiftPixels(scaled, kRight, 262.0f) ==
          doctest::Approx(markerShiftPixels(kQuad, kRight, 131.0f)));
}

TEST_CASE("an unknown distance or an empty parallax moves nothing") {
    CHECK(markerShiftPixels(kQuad, kRight, 0.0f) == 0.0f);
    CHECK(markerShiftPixels(kQuad, kRight, -4.0f) == 0.0f);
    CHECK(markerShiftPixels(MarkerParallax{}, kRight, 50.0f) == 0.0f);
    CHECK(markerShiftPixels(MarkerParallax{1.5f, 640.0f, 0.0f}, kRight, 50.0f) == 0.0f);
}

TEST_CASE("each eye's copies move every marker by its own distance's shift") {
    const std::vector<MarkerSpot> spots{{0.3f, 0.5f, 131.0f}, {0.7f, 0.5f, 1.5f}};
    const std::vector<MarkerCopy> left = markerEyeCopies(spots, kBand, kQuad, kLeft);
    const std::vector<MarkerCopy> right = markerEyeCopies(spots, kBand, kQuad, kRight);
    REQUIRE(left.size() == 2);
    REQUIRE(right.size() == 2);
    CHECK(left[0].dx == -20);
    CHECK(right[0].dx == 20);
    CHECK(left[1].dx == 0);
    CHECK(right[1].dx == 0);
    // The source is the box the game drew, whatever the shift.
    const std::vector<PixelRect> boxes = markerBoxes({spots[0]}, kBand);
    REQUIRE(boxes.size() == 1);
    CHECK(left[0].source.x == boxes[0].x);
    CHECK(left[0].source.width == boxes[0].width);
    CHECK(right[0].source.y == boxes[0].y);
}

TEST_CASE("overlapping markers at different distances keep their own copies") {
    const std::vector<MarkerSpot> spots{{0.5f, 0.5f, 100.0f}, {0.51f, 0.5f, 2.0f}};
    CHECK(markerBoxes(spots, kBand).size() == 1);
    const std::vector<MarkerCopy> right = markerEyeCopies(spots, kBand, kQuad, kRight);
    REQUIRE(right.size() == 2);
    CHECK(right[0].dx != right[1].dx);
}

TEST_CASE("a copy is trimmed to land inside the band") {
    // A marker at the band's right edge moved further right loses the columns past the edge.
    const std::vector<MarkerCopy> right = markerEyeCopies({{1.0f, 0.5f, 200.0f}}, kBand, kQuad, kRight);
    REQUIRE(right.size() == 1);
    const MarkerCopy& c = right[0];
    CHECK(c.source.x + static_cast<std::int64_t>(c.source.width) + c.dx <= 1280);
    // Moved left, its left end stays inside.
    const std::vector<MarkerCopy> left = markerEyeCopies({{0.0f, 0.5f, 200.0f}}, kBand, kQuad, kLeft);
    REQUIRE(left.size() == 1);
    CHECK(left[0].source.x + left[0].dx >= 0);
    // A spot outside the band has none.
    CHECK(markerEyeCopies({{1.2f, 0.5f, 200.0f}}, kBand, kQuad, kRight).empty());
}
