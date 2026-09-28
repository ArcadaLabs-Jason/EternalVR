#include "engine/eternal/resolver/pattern.hpp"

#include "pe_builder.hpp"

#include <doctest/doctest.h>

#include <initializer_list>
#include <ostream>
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
