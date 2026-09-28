#include "engine/eternal/resolver/pe_image.hpp"

#include "pe_builder.hpp"

#include <doctest/doctest.h>

#include <cstring>
#include <ostream>
#include <span>

using evr::resolver::PeError;
using evr::resolver::PeImage;
using evr::test::PeSpec;
using evr::test::SectionSpec;

namespace {

constexpr std::uint32_t kTextRva = 0x1000;
constexpr std::uint32_t kRdataRva = 0x3000;
constexpr std::uint32_t kBssLikeRva = 0x5000;

PeSpec twoSectionSpec() {
    PeSpec spec;
    SectionSpec text{".text", kTextRva, 0, {}};
    evr::test::appendBytes(text.data, {0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xC3});
    SectionSpec rdata{".rdata", kRdataRva, 0, {}};
    evr::test::appendAscii(rdata.data, "hello");
    // Mostly virtual: 16 bytes on disk (padded to the file alignment), 0x2000 bytes in memory.
    SectionSpec data{".data", kBssLikeRva, 0x2000, std::vector<std::uint8_t>(16, 0xAB)};
    spec.sections = {text, rdata, data};
    return spec;
}

PeError parseError(const std::vector<std::byte>& bytes) {
    const auto result = PeImage::parse(bytes);
    REQUIRE_FALSE(result.ok());
    return result.error().code;
}

void writeU16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::byte>(value >> 8);
}

void writeU32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFF);
    }
}

// Header field offsets in images from pe_builder.
constexpr std::size_t kSizeOfOptionalHeaderField = evr::test::kPeHeaderOffset + 4 + 16;
constexpr std::size_t kOptionalHeader = evr::test::kPeHeaderOffset + 4 + 20;
constexpr std::size_t kSizeOfHeadersField = kOptionalHeader + 60;
constexpr std::size_t kSectionTable = kOptionalHeader + 240;

constexpr std::size_t sectionField(std::size_t index, std::size_t fieldOffset) {
    return kSectionTable + index * 40 + fieldOffset;
}
constexpr std::size_t kVirtualSizeField = 8;
constexpr std::size_t kVirtualAddressField = 12;
constexpr std::size_t kPointerToRawDataField = 20;

} // namespace

TEST_CASE("parses headers and the section table") {
    const PeSpec spec = twoSectionSpec();
    const auto bytes = evr::test::buildPe(spec);
    const auto image = PeImage::parse(bytes);
    REQUIRE(image.ok());

    CHECK(image->imageBase() == spec.imageBase);
    CHECK(image->sizeOfHeaders() == evr::test::kSizeOfHeaders);
    REQUIRE(image->sections().size() == 3);
    CHECK(image->sections()[0].name == ".text");
    CHECK(image->sections()[1].name == ".rdata");

    const auto* rdata = image->findSection(".rdata");
    REQUIRE(rdata != nullptr);
    CHECK(rdata->virtualAddress == kRdataRva);
    CHECK(rdata->rawOffset == evr::test::sectionFileOffset(spec, 1));
    CHECK(image->findSection(".reloc") == nullptr);
}

TEST_CASE("RVA and file offset conversions round-trip") {
    const PeSpec spec = twoSectionSpec();
    const auto bytes = evr::test::buildPe(spec);
    const auto image = PeImage::parse(bytes);
    REQUIRE(image.ok());

    const std::uint32_t rdataOffset = evr::test::sectionFileOffset(spec, 1);
    CHECK(image->rvaToOffset(kRdataRva + 2) == rdataOffset + 2);
    CHECK(image->offsetToRva(rdataOffset + 2) == kRdataRva + 2);

    // Headers map one-to-one.
    CHECK(image->rvaToOffset(0x3C) == 0x3C);
    CHECK(image->offsetToRva(0x3C) == 0x3Cu);

    // The string is readable through its RVA.
    const auto hello = image->bytesAtRva(kRdataRva, 5);
    REQUIRE(hello.has_value());
    CHECK(std::memcmp(hello->data(), "hello", 5) == 0);
}

TEST_CASE("RVAs without file bytes do not map") {
    const auto bytes = evr::test::buildPe(twoSectionSpec());
    const auto image = PeImage::parse(bytes);
    REQUIRE(image.ok());

    // Between sections.
    CHECK_FALSE(image->rvaToOffset(0x2000).has_value());
    // In the zero-filled virtual tail of .data, beyond its file-backed bytes.
    CHECK_FALSE(image->rvaToOffset(kBssLikeRva + 0x1000).has_value());
    CHECK(image->findSection(".data")->containsRva(kBssLikeRva + 0x1000));
    // Past the end of the file.
    CHECK_FALSE(image->offsetToRva(bytes.size()).has_value());
}

TEST_CASE("byte ranges may not run off the end of a section") {
    const auto bytes = evr::test::buildPe(twoSectionSpec());
    const auto image = PeImage::parse(bytes);
    REQUIRE(image.ok());

    const auto* text = image->findSection(".text");
    REQUIRE(text != nullptr);
    CHECK(image->bytesAtRva(kTextRva, text->fileBackedSize()).has_value());
    CHECK_FALSE(image->bytesAtRva(kTextRva, text->fileBackedSize() + 1).has_value());
    CHECK_FALSE(image->bytesAtRva(0xFFFFFFF0u, 0x100).has_value());
}

TEST_CASE("every truncation of a valid image is rejected without reading out of bounds") {
    const auto bytes = evr::test::buildPe(twoSectionSpec());
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        const std::span<const std::byte> prefix(bytes.data(), length);
        const auto result = PeImage::parse(prefix);
        CHECK_FALSE(result.ok());
        if (!result.ok()) {
            const PeError code = result.error().code;
            CHECK((code == PeError::Truncated || code == PeError::SectionOutOfBounds));
        }
    }
}

TEST_CASE("signature and format errors are reported precisely") {
    const PeSpec spec = twoSectionSpec();

    SUBCASE("bad DOS signature") {
        auto bytes = evr::test::buildPe(spec);
        bytes[0] = std::byte{'X'};
        CHECK(parseError(bytes) == PeError::BadDosSignature);
    }
    SUBCASE("bad NT signature") {
        auto bytes = evr::test::buildPe(spec);
        bytes[evr::test::kPeHeaderOffset] = std::byte{'X'};
        CHECK(parseError(bytes) == PeError::BadNtSignature);
    }
    SUBCASE("e_lfanew points past the end") {
        auto bytes = evr::test::buildPe(spec);
        writeU16(bytes, 0x3C, 0xFFFF);
        CHECK(parseError(bytes) == PeError::Truncated);
    }
    SUBCASE("32-bit x86 machine") {
        PeSpec x86 = spec;
        x86.machine = 0x014C;
        CHECK(parseError(evr::test::buildPe(x86)) == PeError::UnsupportedMachine);
    }
    SUBCASE("PE32 optional header") {
        PeSpec pe32 = spec;
        pe32.optionalHeaderMagic = 0x10B;
        CHECK(parseError(evr::test::buildPe(pe32)) == PeError::NotPe32Plus);
    }
    SUBCASE("section data beyond the end of the file") {
        auto bytes = evr::test::buildPe(spec);
        // Drop the last section's data but keep the headers intact.
        bytes.resize(evr::test::sectionFileOffset(spec, 2) + 8);
        CHECK(parseError(bytes) == PeError::SectionOutOfBounds);
    }
}

TEST_CASE("error messages say what went wrong") {
    const auto result = PeImage::parse({});
    REQUIRE_FALSE(result.ok());
    CHECK_FALSE(result.error().message.empty());
}

TEST_CASE("SizeOfHeaders may not hide sections or run past the file") {
    SUBCASE("past the end of the file") {
        auto bytes = evr::test::buildPe(twoSectionSpec());
        writeU32(bytes, kSizeOfHeadersField, static_cast<std::uint32_t>(bytes.size() + 1));
        CHECK(parseError(bytes) == PeError::Truncated);
    }
    SUBCASE("reaching into the first section") {
        // A section big enough that the file extends past its RVA, so only the overlap is wrong.
        PeSpec spec;
        spec.sections = {SectionSpec{".text", 0x1000, 0, std::vector<std::uint8_t>(0x1000, 0xCC)}};
        auto bytes = evr::test::buildPe(spec);
        REQUIRE(PeImage::parse(bytes).ok());
        writeU32(bytes, kSizeOfHeadersField, 0x1200);
        CHECK(parseError(bytes) == PeError::InconsistentLayout);
    }
    SUBCASE("a section placed inside the headers") {
        PeSpec spec;
        spec.sections = {SectionSpec{".text", 0x200, 0, {0xC3}}};
        CHECK(parseError(evr::test::buildPe(spec)) == PeError::InconsistentLayout);
    }
}

TEST_CASE("sections may not overlap") {
    SUBCASE("in memory") {
        PeSpec spec;
        spec.sections = {SectionSpec{".text", 0x1000, 0x800, {0xC3}},
                         SectionSpec{".rdata", 0x1400, 0, {1, 2}}};
        CHECK(parseError(evr::test::buildPe(spec)) == PeError::InconsistentLayout);
    }
    SUBCASE("in the file") {
        const PeSpec spec = twoSectionSpec();
        auto bytes = evr::test::buildPe(spec);
        // Point .rdata's raw data at .text's.
        writeU32(bytes, sectionField(1, kPointerToRawDataField), evr::test::sectionFileOffset(spec, 0));
        CHECK(parseError(bytes) == PeError::InconsistentLayout);
    }
    SUBCASE("adjacent sections are fine") {
        PeSpec spec;
        spec.sections = {SectionSpec{".text", 0x1000, 0x1000, {0xC3}}, SectionSpec{".rdata", 0x2000, 0, {1}}};
        CHECK(PeImage::parse(evr::test::buildPe(spec)).ok());
    }
}

TEST_CASE("a section may not extend past the 32-bit address space") {
    const PeSpec spec = twoSectionSpec();
    auto bytes = evr::test::buildPe(spec);
    writeU32(bytes, sectionField(2, kVirtualAddressField), 0xFFFFF000u);
    writeU32(bytes, sectionField(2, kVirtualSizeField), 0x2000u);
    CHECK(parseError(bytes) == PeError::InconsistentLayout);

    // Ending exactly at 4 GiB is still addressable.
    writeU32(bytes, sectionField(2, kVirtualSizeField), 0x1000u);
    const auto image = PeImage::parse(bytes);
    REQUIRE(image.ok());
    CHECK(image->offsetToRva(evr::test::sectionFileOffset(spec, 2) + 4) == 0xFFFFF004u);
}

TEST_CASE("a section with no file data maps no bytes") {
    PeSpec spec = twoSectionSpec();
    spec.sections.push_back(SectionSpec{".bss", 0x8000, 0x3000, {}});
    const auto bytes = evr::test::buildPe(spec);
    const auto image = PeImage::parse(bytes);
    REQUIRE(image.ok());

    const auto* bss = image->findSection(".bss");
    REQUIRE(bss != nullptr);
    CHECK(bss->rawSize == 0);
    CHECK(bss->containsRva(0x8000));
    CHECK_FALSE(image->rvaToOffset(0x8000).has_value());
    CHECK_FALSE(image->bytesAtRva(0x8000, 1).has_value());
    CHECK(image->sectionBytes(*bss).empty());
}

TEST_CASE("a lying SizeOfOptionalHeader is rejected without reading out of bounds") {
    SUBCASE("too small to hold SizeOfHeaders") {
        auto bytes = evr::test::buildPe(twoSectionSpec());
        writeU16(bytes, kSizeOfOptionalHeaderField, 16);
        CHECK(parseError(bytes) == PeError::Truncated);
    }
    SUBCASE("past the end of the file") {
        auto bytes = evr::test::buildPe(twoSectionSpec());
        writeU16(bytes, kSizeOfOptionalHeaderField, 0xFFFF);
        CHECK(parseError(bytes) == PeError::Truncated);
    }
    SUBCASE("moving the section table onto other data") {
        auto bytes = evr::test::buildPe(twoSectionSpec());
        // The table now starts at the second entry, and its last entry reads what follows the table.
        writeU16(bytes, kSizeOfOptionalHeaderField, 240 + 40);
        const auto result = PeImage::parse(bytes);
        if (result.ok()) {
            // Whatever was read, every section it accepted maps inside the file.
            for (const auto& section : result->sections()) {
                CHECK(result->sectionBytes(section).size() == section.fileBackedSize());
            }
        }
    }
}

TEST_CASE("sections that did not come from the image have no bytes") {
    const auto bytes = evr::test::buildPe(twoSectionSpec());
    const auto image = PeImage::parse(bytes);
    REQUIRE(image.ok());
    evr::resolver::Section foreign;
    foreign.rawOffset = static_cast<std::uint32_t>(bytes.size());
    foreign.rawSize = 0x100;
    CHECK(image->sectionBytes(foreign).empty());
}
