#include "stereo_seq/png_writer.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using evr::stereo_seq::adler32;
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

// Inflates a zlib stream made of stored blocks only.
std::vector<std::uint8_t> inflateStored(const std::vector<std::uint8_t>& z) {
    REQUIRE(z.size() >= 6);
    CHECK(((std::uint32_t{z[0]} << 8) | z[1]) % 31 == 0);
    CHECK((z[0] & 0x0F) == 8);
    std::vector<std::uint8_t> out;
    std::size_t at = 2;
    for (;;) {
        REQUIRE(at + 5 <= z.size());
        const std::uint8_t header = z[at];
        CHECK((header & 0x06) == 0); // stored
        const std::uint16_t len = static_cast<std::uint16_t>(z[at + 1] | (z[at + 2] << 8));
        const std::uint16_t nlen = static_cast<std::uint16_t>(z[at + 3] | (z[at + 4] << 8));
        CHECK(static_cast<std::uint16_t>(~len) == nlen);
        at += 5;
        REQUIRE(at + len <= z.size());
        out.insert(out.end(), z.begin() + static_cast<std::ptrdiff_t>(at),
                   z.begin() + static_cast<std::ptrdiff_t>(at + len));
        at += len;
        if (header & 1) {
            break;
        }
    }
    REQUIRE(at + 4 == z.size());
    CHECK(adler32(out.data(), out.size()) == be32(z.data() + at));
    return out;
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
    const auto raw = inflateStored(c[1].data);
    REQUIRE(raw.size() == h * (1 + w * 3));
    for (std::uint32_t y = 0; y < h; ++y) {
        CHECK(raw[y * (1 + w * 3)] == 0);
        CHECK(std::memcmp(raw.data() + y * (1 + w * 3) + 1, rgb.data() + y * w * 3, w * 3) == 0);
    }
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
    const auto raw = inflateStored(c[1].data);
    REQUIRE(raw.size() == h * (1 + w * 4));
    for (std::uint32_t y = 0; y < h; ++y) {
        CHECK(raw[y * (1 + w * 4)] == 0);
        CHECK(std::memcmp(raw.data() + y * (1 + w * 4) + 1, rgba.data() + y * w * 4, w * 4) == 0);
    }
}

TEST_CASE("png: an image larger than one stored block") {
    const std::uint32_t w = 200;
    const std::uint32_t h = 150; // 90,150 bytes of scanlines: two blocks
    std::vector<std::uint8_t> rgb(w * h * 3, 0x5A);
    const auto c = chunks(encodePngRgb8(rgb.data(), w, h));
    REQUIRE(c.size() == 3);
    const auto raw = inflateStored(c[1].data);
    CHECK(raw.size() == h * (1 + w * 3));
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
