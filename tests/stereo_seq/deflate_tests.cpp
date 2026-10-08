#include "stereo_seq/deflate.hpp"

#include "support/inflate.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using evr::stereo_seq::adler32;
using evr::stereo_seq::Compression;
using evr::stereo_seq::zlibCompress;
using evr::test::Inflated;
using evr::test::inflateZlib;

namespace {

// xorshift: the same bytes on every platform.
std::vector<std::uint8_t> noise(std::size_t n, std::uint32_t seed = 0x9E3779B9u) {
    std::vector<std::uint8_t> v(n);
    std::uint32_t s = seed;
    for (auto& b : v) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        b = static_cast<std::uint8_t>(s >> 24);
    }
    return v;
}

// Compresses, inflates with the reference decoder and checks the bytes and the Adler-32 come back.
Inflated roundTrip(const std::vector<std::uint8_t>& data) {
    const auto z = zlibCompress(data.data(), data.size());
    const Inflated back = inflateZlib(z);
    REQUIRE(back.ok);
    CHECK(back.bytes == data);
    CHECK(back.adler == adler32(data.data(), data.size()));
    return back;
}

} // namespace

TEST_CASE("deflate: the zlib header") {
    const std::vector<std::uint8_t> data{1, 2, 3};
    const auto z = zlibCompress(data.data(), data.size());
    REQUIRE(z.size() >= 6);
    CHECK((z[0] & 0x0F) == 8); // deflate
    CHECK((z[0] >> 4) == 7);   // 32 KB window
    CHECK((z[1] & 0x20) == 0); // no preset dictionary
    CHECK(((z[0] << 8) | z[1]) % 31 == 0);
}

TEST_CASE("deflate: stored streams round-trip uncompressed") {
    for (const std::size_t n :
         {std::size_t{0}, std::size_t{1}, std::size_t{65535}, std::size_t{65536}, std::size_t{200000}}) {
        const auto data = noise(n, 0x1234u + static_cast<std::uint32_t>(n));
        const auto z = zlibCompress(data.data(), data.size(), Compression::Stored);
        REQUIRE(z.size() >= 2);
        CHECK(z[0] == 0x78);
        CHECK(z[1] == 0x01);
        CHECK(((z[0] << 8) | z[1]) % 31 == 0);
        // 2 header bytes, 5 per block of up to 65535 bytes (one empty block for no input), 4 of Adler-32.
        const std::size_t blocks = n == 0 ? 1 : (n + 65534) / 65535;
        CHECK(z.size() == 2 + n + 5 * blocks + 4);
        const Inflated back = inflateZlib(z);
        REQUIRE(back.ok);
        CHECK(back.bytes == data);
        CHECK(back.adler == adler32(data.data(), data.size()));
    }
}

TEST_CASE("deflate: empty and tiny inputs") {
    roundTrip({});
    roundTrip({0});
    roundTrip({0xFF, 0xFF});
    roundTrip({7, 7, 7, 7, 7}); // one match of four after a literal
    const std::string text = "the quick brown fox jumps over the lazy dog; the quick brown fox jumps again";
    const auto back = roundTrip(std::vector<std::uint8_t>(text.begin(), text.end()));
    CHECK(back.fixed + back.dynamic == 1); // a short text is cheaper coded than stored
}

TEST_CASE("deflate: long runs use the longest matches") {
    const std::vector<std::uint8_t> zeros(1 << 20, 0);
    const auto z = zlibCompress(zeros.data(), zeros.size());
    CHECK(z.size() < 4096); // about 4064 matches of 258
    const auto back = inflateZlib(z);
    REQUIRE(back.ok);
    CHECK(back.bytes == zeros);
}

TEST_CASE("deflate: data that does not compress is stored") {
    const auto data = noise(300000);
    const auto z = zlibCompress(data.data(), data.size());
    CHECK(z.size() <= data.size() + data.size() / 65535 * 5 + 64);
    const auto back = inflateZlib(z);
    REQUIRE(back.ok);
    CHECK(back.bytes == data);
    CHECK(back.stored >= 5);
}

TEST_CASE("deflate: matches across the window") {
    for (const std::size_t period :
         {std::size_t{259}, std::size_t{4099}, std::size_t{32767}, std::size_t{32768}, std::size_t{40000}}) {
        CAPTURE(period);
        const auto block = noise(period, static_cast<std::uint32_t>(period));
        std::vector<std::uint8_t> data;
        for (int i = 0; i < 4; ++i) {
            data.insert(data.end(), block.begin(), block.end());
        }
        const auto z = zlibCompress(data.data(), data.size());
        const auto back = inflateZlib(z);
        REQUIRE(back.ok);
        CHECK(back.bytes == data);
        if (period < 32768) {
            CHECK(z.size() < period + period / 4); // the repeats are matches
        }
    }
}

TEST_CASE("deflate: skewed data spans several dynamic blocks") {
    // Mostly small values with runs, like filtered image rows: well over one block of symbols.
    const auto bits = noise(600000, 12345);
    std::vector<std::uint8_t> data(bits.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        const std::uint8_t r = bits[i];
        data[i] = r < 160 ? 0 : r < 220 ? static_cast<std::uint8_t>(r & 3) : static_cast<std::uint8_t>(r);
    }
    const auto z = zlibCompress(data.data(), data.size());
    CHECK(z.size() < data.size() / 2);
    const auto back = inflateZlib(z);
    REQUIRE(back.ok);
    CHECK(back.bytes == data);
    CHECK(back.dynamic >= 2);
}

TEST_CASE("deflate: every byte value and every match length") {
    std::vector<std::uint8_t> data;
    for (int v = 0; v < 256; ++v) {
        data.push_back(static_cast<std::uint8_t>(v));
    }
    const auto unique = noise(300, 77);
    for (std::size_t length = 3; length <= 300; ++length) {
        data.insert(data.end(), unique.begin(), unique.begin() + static_cast<std::ptrdiff_t>(length));
        data.push_back(static_cast<std::uint8_t>(length)); // ends the match here
    }
    roundTrip(data);
}
