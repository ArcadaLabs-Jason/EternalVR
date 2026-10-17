#include "engine/eternal/resolver/pattern.hpp"

#include "pe_builder.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <ostream>
#include <utility>
#include <vector>

using evr::resolver::Pattern;
using evr::resolver::PatternError;

namespace {

std::vector<std::byte> toBytes(std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> bytes;
    for (const std::uint8_t v : values) {
        bytes.push_back(static_cast<std::byte>(v));
    }
    return bytes;
}

PatternError parseError(std::string_view text) {
    const auto result = Pattern::parse(text);
    REQUIRE_FALSE(result.ok());
    return result.error().code;
}

} // namespace

TEST_CASE("parses hex bytes and both wildcard spellings") {
    const auto pattern = Pattern::parse("48 8b ?? 05 ?");
    REQUIRE(pattern.ok());
    CHECK(pattern->size() == 5);

    const auto bytes = toBytes({0x48, 0x8B, 0x77, 0x05, 0x99});
    CHECK(pattern->matchesAt(bytes, 0));
}

TEST_CASE("extra whitespace between tokens is ignored") {
    const auto pattern = Pattern::parse("  48\t8B \n 05  ");
    REQUIRE(pattern.ok());
    CHECK(pattern->size() == 3);
}

TEST_CASE("malformed patterns are rejected") {
    CHECK(parseError("") == PatternError::Empty);
    CHECK(parseError("   ") == PatternError::Empty);
    CHECK(parseError("4G") == PatternError::InvalidToken);
    CHECK(parseError("488B") == PatternError::InvalidToken);
    CHECK(parseError("4") == PatternError::InvalidToken);
    CHECK(parseError("48 ???") == PatternError::InvalidToken);
    CHECK(parseError("?? ?") == PatternError::AllWildcards);
}

TEST_CASE("findAll reports every match including overlapping ones") {
    const auto pattern = Pattern::parse("AA ?? AA");
    REQUIRE(pattern.ok());
    const auto bytes = toBytes({0xAA, 0x01, 0xAA, 0x02, 0xAA, 0x00});
    CHECK(evr::resolver::findAll(bytes, *pattern) == std::vector<std::size_t>{0, 2});
}

TEST_CASE("matches never read past the end") {
    const auto pattern = Pattern::parse("01 02 03");
    REQUIRE(pattern.ok());
    const auto bytes = toBytes({0x00, 0x01, 0x02});
    CHECK(evr::resolver::findAll(bytes, *pattern).empty());
    CHECK_FALSE(pattern->matchesAt(bytes, 1));
    CHECK_FALSE(pattern->matchesAt(bytes, 100));
    CHECK(evr::resolver::findAll({}, *pattern).empty());
}

TEST_CASE("scanSection returns RVAs within the section") {
    evr::test::PeSpec spec;
    evr::test::SectionSpec text{".text", 0x1000, 0, {}};
    evr::test::appendBytes(text.data, {0x90, 0x90, 0x48, 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00, 0xC3});
    spec.sections = {text};
    const auto bytes = evr::test::buildPe(spec);
    const auto image = evr::resolver::PeImage::parse(bytes);
    REQUIRE(image.ok());

    const auto pattern = Pattern::parse("48 8B 05 ?? ?? ?? ??");
    REQUIRE(pattern.ok());
    const auto rvas = evr::resolver::scanSection(*image, *image->findSection(".text"), *pattern);
    CHECK(rvas == std::vector<std::uint32_t>{0x1002});
}

TEST_CASE("scanSection skips matches whose RVA would not fit in 32 bits") {
    evr::test::PeSpec spec;
    evr::test::SectionSpec text{".text", 0x1000, 0, {0x90, 0x90, 0xC3}};
    spec.sections = {text};
    const auto bytes = evr::test::buildPe(spec);
    const auto image = evr::resolver::PeImage::parse(bytes);
    REQUIRE(image.ok());

    // A hand-built section near the top of the address space, over the same file bytes.
    evr::resolver::Section high = *image->findSection(".text");
    high.virtualAddress = 0xFFFFFFFEu;
    const auto pattern = Pattern::parse("C3");
    REQUIRE(pattern.ok());
    CHECK(evr::resolver::scanSection(*image, high, *pattern).empty());
    const auto nops = Pattern::parse("90");
    REQUIRE(nops.ok());
    CHECK(evr::resolver::scanSection(*image, high, *nops) ==
          std::vector<std::uint32_t>{0xFFFFFFFEu, 0xFFFFFFFFu});
}

TEST_CASE("the anchor is the least common fixed byte in game code, the first of equals") {
    // 48 (REX.W) is about 8% of the game's code and A2 well under 0.1%: findAll looks for the A2.
    const auto rare = Pattern::parse("48 8B 05 ?? ?? ?? ?? A2");
    REQUIRE(rare.ok());
    CHECK(rare->anchor() == std::pair<std::size_t, std::byte>{7, std::byte{0xA2}});

    const auto wildFirst = Pattern::parse("?? 48 ?? 48");
    REQUIRE(wildFirst.ok());
    CHECK(wildFirst->anchor() == std::pair<std::size_t, std::byte>{1, std::byte{0x48}});
}

TEST_CASE("findAll returns exactly the offsets where the pattern matches") {
    // Bytes drawn from a few common values so partial matches are frequent, with copies of each
    // pattern planted at the very start, at the very end and overlapping.
    std::vector<std::byte> bytes(4096);
    std::uint32_t state = 12345;
    const std::uint8_t common[] = {0x48, 0x8B, 0x05, 0x00, 0xA2, 0xE8, 0x89, 0xC3};
    for (std::byte& b : bytes) {
        state = state * 1664525u + 1013904223u;
        b = static_cast<std::byte>(common[(state >> 24) % 8]);
    }
    const auto plant = [&bytes](std::size_t at, std::initializer_list<std::uint8_t> values) {
        for (const std::uint8_t v : values) {
            bytes[at++] = static_cast<std::byte>(v);
        }
    };
    plant(0, {0x48, 0x8B, 0x05, 0x11, 0x22, 0x33, 0x44, 0xA2});
    plant(bytes.size() - 8, {0x48, 0x8B, 0x05, 0x55, 0x66, 0x77, 0x88, 0xA2});
    plant(2000, {0xA2, 0x48, 0xA2, 0x48, 0xA2});

    for (const char* text : {"48 8B 05 ?? ?? ?? ?? A2", "A2 48 A2", "?? 48 ?? A2", "05", "A2 ??", "?? ?? A2",
                             "E8 ?? ?? ?? ?? 48 8B", "C3 00 00 89"}) {
        const auto pattern = Pattern::parse(text);
        REQUIRE(pattern.ok());
        std::vector<std::size_t> expected;
        for (std::size_t offset = 0; offset < bytes.size(); ++offset) {
            if (pattern->matchesAt(bytes, offset)) {
                expected.push_back(offset);
            }
        }
        CAPTURE(text);
        CHECK(evr::resolver::findAll(bytes, *pattern) == expected);
    }
    const auto planted = Pattern::parse("48 8B 05 ?? ?? ?? ?? A2");
    REQUIRE(planted.ok());
    const auto matches = evr::resolver::findAll(bytes, *planted);
    REQUIRE(matches.size() >= 2);
    CHECK(matches.front() == 0);
    CHECK(matches.back() == bytes.size() - 8);
}

TEST_CASE("a match at the start whose anchor is not its first byte") {
    // The anchor (A2) is at 1 in the pattern and at 0 in the bytes: a scan that starts its anchor search
    // anywhere but the anchor's own position, or takes that 0 as a start, gets this wrong.
    const auto pattern = Pattern::parse("48 A2");
    REQUIRE(pattern.ok());
    REQUIRE(pattern->anchor().first == 1);
    const auto bytes = toBytes({0xA2, 0x48, 0xA2, 0x48, 0xA2});
    CHECK(evr::resolver::findAll(bytes, *pattern) == std::vector<std::size_t>{1, 3});
    const auto atZero = toBytes({0x48, 0xA2, 0xA2});
    CHECK(evr::resolver::findAll(atZero, *pattern) == std::vector<std::size_t>{0});
}
