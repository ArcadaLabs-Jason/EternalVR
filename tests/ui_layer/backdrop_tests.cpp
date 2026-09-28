#include "ui_layer/backdrop.hpp"
#include "ui_layer/ui_settings.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>

using evr::ui_layer::BackdropDetector;
using evr::ui_layer::backdropProbePoints;
using evr::ui_layer::copyRegionsWithoutCentre;
using evr::ui_layer::kBackdropProbePoints;
using evr::ui_layer::readingShowsBackdrop;

namespace {

using Reading = std::array<std::uint8_t, kBackdropProbePoints * 4>;

// A reading whose first `opaque` points have alpha `alpha` and the rest 0.
Reading reading(std::size_t opaque, std::uint8_t alpha = 255) {
    Reading r{};
    for (std::size_t i = 0; i < opaque; ++i) {
        r[i * 4 + 3] = alpha;
    }
    return r;
}

bool inRect(const evr::ui_layer::PixelRect& r, std::uint32_t x, std::uint32_t y) {
    return x >= static_cast<std::uint32_t>(r.x) && x < static_cast<std::uint32_t>(r.x) + r.width &&
           y >= static_cast<std::uint32_t>(r.y) && y < static_cast<std::uint32_t>(r.y) + r.height;
}

} // namespace

TEST_CASE("backdrop probe: eight points just outside the masked square") {
    const std::uint32_t w = 2560;
    const std::uint32_t h = 2100;
    const auto points = backdropProbePoints(w, h, 0.1f, 4);
    REQUIRE(points);
    // The square is 210 px: columns 1175..1384, rows 945..1154; the points sit 4 px beyond it.
    CHECK((*points)[0].x == 1170);
    CHECK((*points)[0].y == 940);
    CHECK((*points)[2].x == 1389);
    CHECK((*points)[4].y == 1159);
    CHECK((*points)[1].x == 1280);
    CHECK((*points)[3].y == 1050);
    // Every point is copied (outside the square), none inside it.
    const auto regions = copyRegionsWithoutCentre(w, h, 0.1f);
    for (const auto& p : *points) {
        CHECK(p.x < w);
        CHECK(p.y < h);
        bool copied = false;
        for (const auto& r : regions) {
            copied = copied || inRect(r, p.x, p.y);
        }
        CHECK(copied);
        CHECK_FALSE((p.x >= 1175 && p.x <= 1384 && p.y >= 945 && p.y <= 1154));
    }
}

TEST_CASE("backdrop probe: no points without a square or when the ring leaves the target") {
    CHECK_FALSE(backdropProbePoints(2560, 2100, 0.0f, 4));
    CHECK_FALSE(backdropProbePoints(100, 100, 2.0f, 4));
    CHECK_FALSE(backdropProbePoints(100, 100, 0.9f, 10));
    CHECK(backdropProbePoints(100, 100, 0.5f, 10));
}

TEST_CASE("backdrop probe: a reading shows a backdrop when six of eight points are opaque") {
    CHECK(readingShowsBackdrop(reading(8)));
    CHECK(readingShowsBackdrop(reading(6)));
    CHECK_FALSE(readingShowsBackdrop(reading(5)));
    CHECK(readingShowsBackdrop(reading(8, 239))); // a fade on its way out, as logged on the rig
    CHECK(readingShowsBackdrop(reading(8, 32)));
    CHECK_FALSE(readingShowsBackdrop(reading(8, 31)));
    CHECK_FALSE(readingShowsBackdrop(reading(0)));
    // Colour does not matter: the backdrop and the fade are near black, alpha 255.
    Reading black = reading(8);
    black[0] = 3;
    CHECK(readingShowsBackdrop(black));
}

TEST_CASE("backdrop detector: up after two readings, gone after eight") {
    BackdropDetector d;
    CHECK_FALSE(d.backdrop());
    CHECK_FALSE(d.update(true));
    CHECK_FALSE(d.backdrop());
    CHECK(d.update(true));
    CHECK(d.backdrop());
    for (int i = 1; i < BackdropDetector::kOffReadings; ++i) {
        CHECK_FALSE(d.update(false));
        CHECK(d.backdrop());
    }
    CHECK(d.update(false));
    CHECK_FALSE(d.backdrop());
}

TEST_CASE("backdrop detector: a single disagreeing reading does not flip it") {
    BackdropDetector d;
    d.update(true);
    d.update(false); // the run starts again
    CHECK_FALSE(d.update(true));
    CHECK(d.update(true));
    for (int i = 0; i < 20; ++i) {
        // A clear reading now and then while the backdrop stays up.
        CHECK_FALSE(d.update(i % 5 == 0 ? false : true));
        CHECK(d.backdrop());
    }
    d.reset();
    CHECK_FALSE(d.backdrop());
}
