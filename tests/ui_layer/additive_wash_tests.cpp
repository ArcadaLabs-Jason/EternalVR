#include "ui_layer/additive_wash.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using evr::ui_layer::kWashBlock;
using evr::ui_layer::removeAdditiveWash;
using evr::ui_layer::washBlocks;

namespace {

struct Image {
    std::uint32_t width;
    std::uint32_t height;
    std::vector<std::uint8_t> rgba;

    Image(std::uint32_t w, std::uint32_t h)
        : width(w), height(h), rgba(static_cast<std::size_t>(w) * h * 4) {}

    std::uint8_t* at(std::uint32_t x, std::uint32_t y) {
        return &rgba[(static_cast<std::size_t>(y) * width + x) * 4];
    }
    void
    set(std::uint32_t x, std::uint32_t y, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
        std::uint8_t* p = at(x, y);
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = a;
    }
    void run() { removeAdditiveWash(rgba, width, height); }
};

// The low-health vignette as captured: red (1, 0.315, 0.112) times a level, alpha 0; the level grows
// evenly from 0 at the centre to `edge` at the corners (93 over about 1500 pixels in the capture at
// 2056x2216, a few levels per block).
void addVignette(Image& img, float edge) {
    const float cx = static_cast<float>(img.width) / 2.0f;
    const float cy = static_cast<float>(img.height) / 2.0f;
    const float reach = std::sqrt(cx * cx + cy * cy);
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const float d = std::hypot(static_cast<float>(x) - cx, static_cast<float>(y) - cy) / reach;
            const float level = edge * d;
            std::uint8_t* p = img.at(x, y);
            const auto add = [](std::uint8_t v, float l) {
                return static_cast<std::uint8_t>(std::min(255.0f, static_cast<float>(v) + std::round(l)));
            };
            p[0] = add(p[0], level);
            p[1] = add(p[1], level * 0.315f);
            p[2] = add(p[2], level * 0.112f);
        }
    }
}

int maxExcess(Image& img, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
    int most = 0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            const std::uint8_t* p = img.at(x, y);
            for (int c = 0; c < 3; ++c) {
                most = std::max(most, p[c] - p[3]);
            }
        }
    }
    return most;
}

// What the rule may leave or take beyond the exact wash: the vignette's change across a block (about 4
// levels at the capture's gradient) and rounding.
constexpr int kTolerance = 6;

} // namespace

TEST_CASE("additive wash: block counts round up") {
    CHECK(washBlocks(0) == 0);
    CHECK(washBlocks(1) == 1);
    CHECK(washBlocks(kWashBlock) == 1);
    CHECK(washBlocks(kWashBlock + 1) == 2);
    CHECK(washBlocks(2056) == 33);
}

TEST_CASE("additive wash: a premultiplied HUD with its small additive pieces is unchanged") {
    Image img(300, 200);
    // A panel with coverage and colour within it, a soft shadow, and a 30-pixel additive icon (alpha 0).
    for (std::uint32_t y = 150; y < 180; ++y) {
        for (std::uint32_t x = 20; x < 200; ++x) {
            img.set(x, y, 90, 40, 20, 120);
        }
    }
    for (std::uint32_t x = 20; x < 200; ++x) {
        img.set(x, 181, 0, 0, 0, 30);
    }
    for (std::uint32_t y = 100; y < 130; ++y) {
        for (std::uint32_t x = 220; x < 250; ++x) {
            img.set(x, y, 40, 200, 230, 0);
        }
    }
    // A glow brighter than its coverage on the panel (the health pips).
    img.set(60, 160, 30, 120, 140, 45);
    const std::vector<std::uint8_t> before = img.rgba;
    img.run();
    CHECK(img.rgba == before);
}

TEST_CASE("additive wash: the full-screen vignette is removed, alpha is kept") {
    Image img(2056, 2216);
    addVignette(img, 93.0f);
    CHECK(maxExcess(img, 0, 0, img.width, img.height) >= 90);
    img.run();
    // Left: at most the vignette's change across a block, at the image's corners.
    CHECK(maxExcess(img, 0, 0, img.width, img.height) <= kTolerance);
    for (std::size_t p = 0; p < img.rgba.size(); p += 4) {
        CHECK(img.rgba[p + 3] == 0);
    }
}

TEST_CASE("additive wash: a small additive icon on the vignette keeps its own light") {
    Image img(640, 480);
    addVignette(img, 20.0f); // the capture's gradient on a smaller image
    const std::uint32_t x0 = 40;
    const std::uint32_t y0 = 40;
    std::vector<std::uint8_t> vignette(img.rgba.begin(), img.rgba.end());
    for (std::uint32_t y = y0; y < y0 + 30; ++y) {
        for (std::uint32_t x = x0; x < x0 + 30; ++x) {
            std::uint8_t* p = img.at(x, y);
            p[0] = static_cast<std::uint8_t>(std::min(255, p[0] + 40));
            p[1] = static_cast<std::uint8_t>(std::min(255, p[1] + 200));
            p[2] = static_cast<std::uint8_t>(std::min(255, p[2] + 230));
        }
    }
    img.run();
    for (std::uint32_t y = y0; y < y0 + 30; ++y) {
        for (std::uint32_t x = x0; x < x0 + 30; ++x) {
            const std::uint8_t* p = img.at(x, y);
            // The icon's own colour back, within the vignette's change across a block.
            CHECK(std::abs(p[0] - 40) <= kTolerance);
            CHECK(std::abs(p[1] - 200) <= kTolerance);
            CHECK(std::abs(p[2] - 230) <= kTolerance);
        }
    }
    // Around the icon the vignette is gone.
    CHECK(maxExcess(img, 0, 0, img.width, y0) <= kTolerance);
}

TEST_CASE("additive wash: over a HUD panel the colour is never taken below its coverage") {
    Image img(256, 256);
    addVignette(img, 90.0f);
    for (std::uint32_t y = 100; y < 110; ++y) {
        for (std::uint32_t x = 0; x < 256; ++x) {
            std::uint8_t* p = img.at(x, y);
            p[3] = 100;
            p[0] = static_cast<std::uint8_t>(std::min(255, p[0] + 60));
        }
    }
    const std::vector<std::uint8_t> before = img.rgba;
    img.run();
    for (std::size_t p = 0; p < img.rgba.size(); p += 4) {
        for (std::size_t c = 0; c < 3; ++c) {
            CHECK(img.rgba[p + c] <= before[p + c]);
            CHECK(img.rgba[p + c] >= std::min(before[p + c], before[p + 3]));
        }
        CHECK(img.rgba[p + 3] == before[p + 3]);
    }
}

TEST_CASE("additive wash: odd sizes and a too-small buffer") {
    Image img(kWashBlock + 7, kWashBlock * 2 + 1);
    addVignette(img, 6.0f);
    img.run();
    CHECK(maxExcess(img, 0, 0, img.width, img.height) <= kTolerance);
    std::vector<std::uint8_t> small(10, 200);
    removeAdditiveWash(small, 4, 4);
    CHECK(small == std::vector<std::uint8_t>(10, 200));
    removeAdditiveWash({}, 0, 0);
}
