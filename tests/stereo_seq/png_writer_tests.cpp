#include "stereo_seq/png_writer.hpp"

#include "support/inflate.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using evr::stereo_seq::adler32;
using evr::stereo_seq::Compression;
using evr::stereo_seq::crc32;
using evr::stereo_seq::encodePngRgb8;
using evr::stereo_seq::encodePngRgba8;
using evr::stereo_seq::PixelLayout;
using evr::stereo_seq::pixelLayoutOfVkFormat;
using evr::stereo_seq::toRgb8;

namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
}

struct Chunk {
    std::string type;
    std::vector<std::uint8_t> data;
};

// Splits a PNG into chunks, checking the signature and every CRC.
std::vector<Chunk> chunks(const std::vector<std::uint8_t>& png) {
    std::vector<Chunk> out;
    const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    REQUIRE(png.size() >= 8);
    REQUIRE(std::memcmp(png.data(), signature, 8) == 0);
    std::size_t at = 8;
    while (at + 12 <= png.size()) {
        const std::uint32_t length = be32(png.data() + at);
        REQUIRE(at + 12 + length <= png.size());
        Chunk c;
        c.type.assign(reinterpret_cast<const char*>(png.data() + at + 4), 4);
        c.data.assign(png.begin() + static_cast<std::ptrdiff_t>(at + 8),
                      png.begin() + static_cast<std::ptrdiff_t>(at + 8 + length));
        CHECK(crc32(png.data() + at + 4, 4 + length) == be32(png.data() + at + 8 + length));
        out.push_back(c);
        at += 12 + length;
    }
    CHECK(at == png.size());
    return out;
}

// The image's rows back from its IDAT chunk: inflated, the Adler-32 checked, unfiltered.
std::vector<std::uint8_t>
decodeRows(const std::vector<std::uint8_t>& idat, std::size_t rowBytes, std::size_t height, std::size_t bpp) {
    const auto z = evr::test::inflateZlib(idat);
    REQUIRE(z.ok);
    CHECK(z.adler == adler32(z.bytes.data(), z.bytes.size()));
    REQUIRE(z.bytes.size() == (rowBytes + 1) * height);
    for (std::size_t y = 0; y < height; ++y) {
        CHECK(z.bytes[y * (rowBytes + 1)] <= 4); // filter type
    }
    const auto rows = evr::test::unfilterPng(z.bytes, rowBytes, height, bpp);
    REQUIRE(rows.size() == rowBytes * height);
    return rows;
}

// The filter type of each scanline.
std::vector<std::uint8_t>
filterTypes(const std::vector<std::uint8_t>& idat, std::size_t rowBytes, std::size_t height) {
    const auto z = evr::test::inflateZlib(idat);
    REQUIRE(z.ok);
    std::vector<std::uint8_t> types;
    for (std::size_t y = 0; y < height && y * (rowBytes + 1) < z.bytes.size(); ++y) {
        types.push_back(z.bytes[y * (rowBytes + 1)]);
    }
    return types;
}

// Paeth's predictor as the PNG specification writes it.
int paethOf(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

} // namespace

TEST_CASE("png: checksums match their reference values") {
    const std::string digits = "123456789";
    CHECK(crc32(reinterpret_cast<const std::uint8_t*>(digits.data()), digits.size()) == 0xCBF43926u);
    const std::string word = "Wikipedia";
    CHECK(adler32(reinterpret_cast<const std::uint8_t*>(word.data()), word.size()) == 0x11E60398u);
    const std::string iend = "IEND";
    CHECK(crc32(reinterpret_cast<const std::uint8_t*>(iend.data()), iend.size()) == 0xAE426082u);
}

TEST_CASE("png: a small image round-trips through the file layout") {
    const std::uint32_t w = 3;
    const std::uint32_t h = 2;
    std::vector<std::uint8_t> rgb(w * h * 3);
    for (std::size_t i = 0; i < rgb.size(); ++i) {
        rgb[i] = static_cast<std::uint8_t>(i * 7);
    }
    const auto png = encodePngRgb8(rgb.data(), w, h);
    const auto c = chunks(png);
    REQUIRE(c.size() == 3);
    CHECK(c[0].type == "IHDR");
    CHECK(c[1].type == "IDAT");
    CHECK(c[2].type == "IEND");
    REQUIRE(c[0].data.size() == 13);
    CHECK(be32(c[0].data.data()) == w);
    CHECK(be32(c[0].data.data() + 4) == h);
    CHECK(c[0].data[8] == 8);
    CHECK(c[0].data[9] == 2);
    CHECK(decodeRows(c[1].data, w * 3, h, 3) == rgb);
}

TEST_CASE("png: a stored image has unfiltered rows and the same pixels") {
    const std::uint32_t w = 40;
    const std::uint32_t h = 30;
    std::vector<std::uint8_t> rgb(w * h * 3);
    for (std::size_t i = 0; i < rgb.size(); ++i) {
        rgb[i] = static_cast<std::uint8_t>(i * 13 + i / 7);
    }
    const auto c = chunks(encodePngRgb8(rgb.data(), w, h, Compression::Stored));
    REQUIRE(c.size() == 3);
    REQUIRE(c[1].data.size() >= 2);
    CHECK(c[1].data[0] == 0x78);
    CHECK(c[1].data[1] == 0x01);
    const auto scanlines = evr::test::inflateZlib(c[1].data);
    REQUIRE(scanlines.ok);
    REQUIRE(scanlines.bytes.size() == (w * 3 + 1) * h);
    for (std::uint32_t y = 0; y < h; ++y) {
        CHECK(scanlines.bytes[y * (w * 3 + 1)] == 0); // filter None
    }
    CHECK(decodeRows(c[1].data, w * 3, h, 3) == rgb);
}

TEST_CASE("png: an RGBA image keeps its alpha channel") {
    const std::uint32_t w = 2;
    const std::uint32_t h = 2;
    const std::vector<std::uint8_t> rgba{10, 20, 30, 0, 40, 50, 60, 128, 70, 80, 90, 255, 1, 2, 3, 4};
    const auto c = chunks(encodePngRgba8(rgba.data(), w, h));
    REQUIRE(c.size() == 3);
    REQUIRE(c[0].data.size() == 13);
    CHECK(c[0].data[8] == 8);
    CHECK(c[0].data[9] == 6);
    CHECK(decodeRows(c[1].data, w * 4, h, 4) == rgba);
}

TEST_CASE("png: a flat image is a small file") {
    const std::uint32_t w = 200;
    const std::uint32_t h = 150;
    std::vector<std::uint8_t> rgb(w * h * 3, 0x5A);
    const auto png = encodePngRgb8(rgb.data(), w, h);
    CHECK(png.size() < 1000); // 90,150 bytes of scanlines
    const auto c = chunks(png);
    REQUIRE(c.size() == 3);
    CHECK(decodeRows(c[1].data, w * 3, h, 3) == rgb);
}

TEST_CASE("png: each row takes the filter that suits it, and every filter round-trips") {
    const std::uint32_t w = 320;
    const std::uint32_t h = 96;
    const std::size_t rowBytes = w * 4;
    std::vector<std::uint8_t> rgba(rowBytes * h);
    std::uint32_t s = 1;
    const auto random = [&s] {
        s = s * 1103515245u + 12345u;
        return static_cast<std::uint8_t>(s >> 16);
    };
    for (std::uint32_t y = 0; y < h; ++y) {
        std::uint8_t* row = rgba.data() + y * rowBytes;
        for (std::uint32_t x = 0; x < w; ++x) {
            std::uint8_t* p = row + x * 4;
            if (y < 32) { // a horizontal ramp from a different start on each row: Sub (Paeth gives the same)
                p[0] = static_cast<std::uint8_t>(x + y * 71);
                p[1] = static_cast<std::uint8_t>(x * 2 + y * 53);
                p[2] = static_cast<std::uint8_t>(y * 97);
                p[3] = 255;
            } else if (y < 64) { // vertical stripes: Up
                p[0] = static_cast<std::uint8_t>((x * 37) ^ (x >> 2));
                p[1] = static_cast<std::uint8_t>(x * 91);
                p[2] = static_cast<std::uint8_t>(x * 13 + 7);
                p[3] = static_cast<std::uint8_t>(x);
            }
        }
        if (y < 64) {
            continue;
        }
        const std::uint8_t* up = row - rowBytes;
        // Noise rows, each followed by a row that Average (y % 4 == 1) or Paeth (y % 4 == 3) predicts
        // exactly. The Paeth row's first pixel is off the one above, so its predictions do not all come
        // from the row above (which would make it a copy of that row: Up).
        for (std::size_t i = 0; i < rowBytes; ++i) {
            const int a = i >= 4 ? row[i - 4] : 0;
            const int b = up[i];
            const int c = i >= 4 ? up[i - 4] : 0;
            const int predicted = y % 4 == 1 ? (a + b) >> 1 : i < 4 ? b + 64 : paethOf(a, b, c);
            row[i] = y % 2 == 0 ? random() : static_cast<std::uint8_t>(predicted);
        }
    }
    const auto png = encodePngRgba8(rgba.data(), w, h);
    CHECK(png.size() < rgba.size() / 2);
    const auto c = chunks(png);
    REQUIRE(c.size() == 3);
    CHECK(decodeRows(c[1].data, rowBytes, h, 4) == rgba);
    const auto types = filterTypes(c[1].data, rowBytes, h);
    REQUIRE(types.size() == h);
    CHECK((types[10] == 1 || types[10] == 4));
    CHECK(types[40] == 2);
    CHECK(types[65] == 3);
    CHECK(types[67] == 4);

    std::vector<std::uint8_t> rgb(w * h * 3);
    for (std::size_t i = 0; i < w * h; ++i) {
        for (int k = 0; k < 3; ++k) {
            rgb[i * 3 + k] = rgba[i * 4 + k];
        }
    }
    const auto c3 = chunks(encodePngRgb8(rgb.data(), w, h));
    REQUIRE(c3.size() == 3);
    CHECK(decodeRows(c3[1].data, w * 3, h, 3) == rgb);
}

TEST_CASE("png: noise round-trips") {
    const std::uint32_t w = 97; // odd sizes
    const std::uint32_t h = 61;
    std::vector<std::uint8_t> rgb(w * h * 3);
    std::uint32_t s = 7;
    for (auto& b : rgb) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        b = static_cast<std::uint8_t>(s);
    }
    const auto png = encodePngRgb8(rgb.data(), w, h);
    CHECK(png.size() < rgb.size() + h + 200); // stored at worst
    const auto c = chunks(png);
    REQUIRE(c.size() == 3);
    CHECK(decodeRows(c[1].data, w * 3, h, 3) == rgb);
}

TEST_CASE("png: swapchain pixels to RGB") {
    SUBCASE("BGRA8 with a row pitch") {
        const std::vector<std::uint8_t> px{10, 20, 30, 255, 40, 50, 60, 255, 0, 0, 0, 0, // row 0 + padding
                                           1,  2,  3,  4,   5,  6,  7,  8,   0, 0, 0, 0};
        const auto rgb = toRgb8(px.data(), 2, 2, 12, PixelLayout::Bgra8);
        const std::vector<std::uint8_t> want{30, 20, 10, 60, 50, 40, 3, 2, 1, 7, 6, 5};
        CHECK(rgb == want);
    }
    SUBCASE("RGBA8") {
        const std::vector<std::uint8_t> px{10, 20, 30, 255};
        CHECK(toRgb8(px.data(), 1, 1, 4, PixelLayout::Rgba8) == std::vector<std::uint8_t>{10, 20, 30});
    }
    SUBCASE("A2B10G10R10: red in the low bits") {
        const std::uint32_t r = 1020;
        const std::uint32_t g = 512;
        const std::uint32_t b = 4;
        const std::uint32_t v = (3u << 30) | (b << 20) | (g << 10) | r;
        std::uint8_t px[4];
        std::memcpy(px, &v, 4);
        CHECK(toRgb8(px, 1, 1, 4, PixelLayout::A2B10G10R10) == std::vector<std::uint8_t>{255, 128, 1});
        CHECK(toRgb8(px, 1, 1, 4, PixelLayout::A2R10G10B10) == std::vector<std::uint8_t>{1, 128, 255});
    }
}

TEST_CASE("png: layouts of the game's swapchain formats") {
    CHECK(pixelLayoutOfVkFormat(44) == PixelLayout::Bgra8); // B8G8R8A8_UNORM
    CHECK(pixelLayoutOfVkFormat(50) == PixelLayout::Bgra8); // B8G8R8A8_SRGB
    CHECK(pixelLayoutOfVkFormat(37) == PixelLayout::Rgba8);
    CHECK(pixelLayoutOfVkFormat(64) == PixelLayout::A2B10G10R10);
    CHECK(pixelLayoutOfVkFormat(58) == PixelLayout::A2R10G10B10);
    CHECK_FALSE(pixelLayoutOfVkFormat(97).has_value()); // R16G16B16A16_SFLOAT
}
