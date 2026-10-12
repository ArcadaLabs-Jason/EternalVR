#include "ui_layer/additive_wash.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using evr::ui_layer::kWashBlock;
using evr::ui_layer::kWashLitRatio;
using evr::ui_layer::removeAdditiveWash;
using evr::ui_layer::washBlocks;
using evr::ui_layer::washBound;
using evr::ui_layer::washShare;

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

// A HUD layer of colour (r, g, b) at coverage `alpha` over [x0, x1) x [y0, y1), premultiplied over what the
// image holds there (the vignette lies behind the HUD).
void addPanel(Image& img,
              std::uint32_t x0,
              std::uint32_t y0,
              std::uint32_t x1,
              std::uint32_t y1,
              std::uint8_t r,
              std::uint8_t g,
              std::uint8_t b,
              std::uint8_t alpha) {
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            std::uint8_t* p = img.at(x, y);
            const std::array<std::uint8_t, 3> own{r, g, b};
            for (std::size_t c = 0; c < 3; ++c) {
                p[c] = static_cast<std::uint8_t>(own[c] + (p[c] * (255 - alpha) + 127) / 255);
            }
            p[3] = static_cast<std::uint8_t>(alpha + (p[3] * (255 - alpha) + 127) / 255);
        }
    }
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

TEST_CASE("additive wash: the bound and the share at their edges") {
    // Uncovered, a pixel's colour is all wash; fully covered, the HUD hides any wash.
    CHECK(washBound(0, 0) == 0);
    CHECK(washBound(17, 0) == 17);
    CHECK(washBound(255, 0) == 255);
    CHECK(washBound(0, 255) == 255);
    CHECK(washBound(9, 255) == 255);
    CHECK(washBound(200, 254) == 255);
    // Colour is the light rounded to a byte: a 1 at coverage 240 can hold up to 25 of wash, a 0 up to 8.
    CHECK(washBound(1, 240) == 25);
    CHECK(washBound(0, 240) == 8);
    CHECK(washBound(10, 128) == 21);
    CHECK(washShare(0, 100) == 0);
    CHECK(washShare(93, 0) == 93);
    CHECK(washShare(255, 0) == 255);
    CHECK(washShare(255, 255) == 0);
    CHECK(washShare(255, 128) == 127);
    // Exhaustively: the bound never reads less than the wash that lit a pixel (so no wash is left for want of
    // precision under high coverage), and taking the bound's share never takes more than the pixel holds.
    int belowWash = 0;
    int pastColour = 0;
    for (int alpha = 0; alpha < 255; ++alpha) {
        for (int v = 0; v < 256; ++v) {
            const auto a = static_cast<std::uint8_t>(alpha);
            const auto u = static_cast<std::uint8_t>(v);
            belowWash += washBound(washShare(u, a), a) < v ? 1 : 0;
            pastColour += washShare(washBound(u, a), a) > v ? 1 : 0;
        }
    }
    CHECK(belowWash == 0);
    CHECK(pastColour == 0);
}

TEST_CASE("additive wash: a block's level counts from exactly two lit blocks to one unlit") {
    // Three by three blocks, all uncovered: 'L' lit by light 40 (red only), 'U' unlit, 'X' fully covered
    // (no information). The middle block is lit; what the rule takes from its centre pixel tells whether its
    // level counted.
    const auto middleAfter = [](const char* layout) {
        Image img(3 * kWashBlock, 3 * kWashBlock);
        for (std::uint32_t y = 0; y < img.height; ++y) {
            for (std::uint32_t x = 0; x < img.width; ++x) {
                const char kind = layout[(y / kWashBlock) * 3 + x / kWashBlock];
                img.set(x, y, kind == 'L' ? 40 : 0, 0, 0, kind == 'X' ? 255 : 0);
            }
        }
        img.run();
        return static_cast<int>(img.at(3 * kWashBlock / 2, 3 * kWashBlock / 2)[0]);
    };
    static_assert(kWashLitRatio == 2);
    CHECK(middleAfter("LLL"
                      "LLL"
                      "UUU") <= 1); // 6 lit, 3 unlit: counts
    CHECK(middleAfter("LLL"
                      "LLX"
                      "UUU") >= 39); // 5 lit, 3 unlit: does not
    CHECK(middleAfter("XLX"
                      "XLX"
                      "XUX") <= 1); // 2 lit, 1 unlit: counts
    CHECK(middleAfter("XXX"
                      "XLX"
                      "XUX") >= 39); // 1 lit, 1 unlit: does not
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

TEST_CASE("additive wash: the vignette behind translucent HUD is removed, solid HUD keeps its colour") {
    // The hit feedback's frame in the corner and the panels' backgrounds: coverage 20 to 60 of 255 with
    // little colour of their own; on the vignette they lit as orange boxes on the HUD quad.
    Image img(2056, 2216);
    addVignette(img, 93.0f);
    addPanel(img, 0, 0, 80, 150, 4, 1, 0, 40);              // a translucent frame in the corner
    addPanel(img, 100, 1900, 400, 1940, 6, 3, 2, 60);       // a panel's background
    addPanel(img, 150, 1910, 250, 1930, 200, 160, 60, 250); // solid HUD on it (digits, icons)
    img.run();
    CHECK(maxExcess(img, 0, 0, img.width, img.height) <= kTolerance);
    for (std::uint32_t y = 0; y < 150; ++y) {
        for (std::uint32_t x = 0; x < 80; ++x) {
            CHECK(img.at(x, y)[0] <= 4 + kTolerance);
            CHECK(img.at(x, y)[3] == 40);
        }
    }
    for (std::uint32_t x = 100; x < 400; ++x) {
        CHECK(img.at(x, 1902)[0] <= 6 + kTolerance);
    }
    for (std::uint32_t x = 150; x < 250; ++x) {
        const std::uint8_t* p = img.at(x, 1920);
        CHECK(std::abs(p[0] - 200) <= 2);
        CHECK(std::abs(p[1] - 160) <= 2);
        CHECK(std::abs(p[2] - 60) <= 2);
    }
}

TEST_CASE("additive wash: colour is only ever taken, never added, and alpha is kept") {
    Image img(256, 256);
    addVignette(img, 90.0f);
    addPanel(img, 0, 100, 256, 110, 60, 20, 10, 100);
    const std::vector<std::uint8_t> before = img.rgba;
    img.run();
    for (std::size_t p = 0; p < img.rgba.size(); p += 4) {
        for (std::size_t c = 0; c < 3; ++c) {
            CHECK(img.rgba[p + c] <= before[p + c]);
        }
        CHECK(img.rgba[p + 3] == before[p + 3]);
    }
}

TEST_CASE("additive wash: a translucent layer over the whole target is left alone") {
    // A fade or tint that covers every pixel has no uncovered block to read a wash from.
    Image img(300, 200);
    addPanel(img, 0, 0, 300, 200, 30, 10, 5, 80);
    const std::vector<std::uint8_t> before = img.rgba;
    img.run();
    CHECK(img.rgba == before);
}

TEST_CASE("additive wash: a glow that fills a block or two, without a wash, is left alone") {
    // The HUD's own additive glow can fill whole blocks; without a vignette the unlit blocks around it
    // outnumber it, so its light stays.
    Image img(640, 480);
    for (std::uint32_t y = 128; y < 192; ++y) {
        for (std::uint32_t x = 128; x < 256; ++x) {
            img.set(x, y, 30, 90, 110, 0);
        }
    }
    addPanel(img, 140, 140, 240, 180, 80, 80, 80, 120);
    const std::vector<std::uint8_t> before = img.rgba;
    img.run();
    CHECK(img.rgba == before);
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

TEST_CASE("additive wash: an additive piece larger than a block keeps no block edges") {
    // The hex pattern behind the LOW HEALTH banner and the panels' glow: additive light over several
    // blocks, patterned and uneven, so the blocks under it have different floors and its bright cells stand
    // above them. What the rule takes from those cells must change smoothly along the piece, not in steps
    // at the block edges (squares on the HUD quad; one level per block stepped by 10 here).
    Image img(640, 480);
    addVignette(img, 20.0f);
    const auto bright = [](std::uint32_t x, std::uint32_t y) {
        return (x / 4 + y / 4) % 2 == 0;
    };
    for (std::uint32_t y = 190; y < 330; ++y) {
        for (std::uint32_t x = 100; x < 420; ++x) {
            const int level = 10 + static_cast<int>(120 * (x - 100) / 320);
            std::uint8_t* p = img.at(x, y);
            p[0] = static_cast<std::uint8_t>(std::min(255, p[0] + (bright(x, y) ? level : level / 2)));
        }
    }
    const std::vector<std::uint8_t> before = img.rgba;
    img.run();
    const std::uint32_t y = 260;
    int steepest = 0;
    int last = -1;
    for (std::uint32_t x = 130; x < 400; ++x) {
        if (!bright(x, y)) {
            continue;
        }
        const std::size_t p = (static_cast<std::size_t>(y) * img.width + x) * 4;
        const int taken = before[p] - img.rgba[p];
        if (last >= 0) {
            steepest = std::max(steepest, std::abs(taken - last));
        }
        last = taken;
    }
    CHECK(steepest <= 2);
}
