#include "engine/eternal/resolver/rip_xref.hpp"

#include "engine/eternal/resolver/string_search.hpp"
#include "pe_builder.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <ostream>
#include <vector>

using evr::resolver::PeImage;
using evr::resolver::RipInstruction;
using evr::resolver::StringQuery;

namespace {

constexpr std::uint32_t kTextRva = 0x1000;
constexpr std::uint32_t kRdataRva = 0x3000;

// Appends a 7-byte RIP-relative instruction at the end of `code` whose operand resolves to
// `targetRva`, given that `code` starts at kTextRva.
void appendRipInstruction(std::vector<std::uint8_t>& code,
                          std::uint8_t rex,
                          std::uint8_t opcode,
                          std::uint8_t modrm,
                          std::uint32_t targetRva) {
    const auto instructionRva = kTextRva + static_cast<std::uint32_t>(code.size());
    const auto nextRva = instructionRva + evr::resolver::kRipInstructionLength;
    evr::test::appendBytes(code, {rex, opcode, modrm});
    // Two's-complement wraparound gives the signed displacement for targets before the instruction.
    evr::test::appendLe32(code, targetRva - nextRva);
}

struct Fixture {
    std::vector<std::byte> bytes;
    std::uint32_t leaRva = 0;
    std::uint32_t movRva = 0;
};

// .text contains, in order:
//   nop padding
//   lea rcx, [rip -> "anchor string"]            (48 8D 0D)  <- match
//   mov r8,  [rip -> "anchor string"]            (4C 8B 05)  <- match
//   lea rdx, [rip -> 16 bytes past the string]   (48 8D 15)  <- wrong target
//   lea rax, [rbp + disp32 -> the string]        (48 8D 85)  <- not RIP-relative (mod = 10)
//   add rax, [rip -> the string]                 (48 03 05)  <- unsupported opcode
//   ret
Fixture buildFixture() {
    evr::test::SectionSpec rdata{".rdata", kRdataRva, 0, {}};
    evr::test::appendAscii(rdata.data, "anchor string");

    evr::test::SectionSpec text{".text", kTextRva, 0, {}};
    evr::test::appendBytes(text.data, {0x90, 0x90, 0x90});

    Fixture fixture;
    fixture.leaRva = kTextRva + static_cast<std::uint32_t>(text.data.size());
    appendRipInstruction(text.data, 0x48, 0x8D, 0x0D, kRdataRva);
    fixture.movRva = kTextRva + static_cast<std::uint32_t>(text.data.size());
    appendRipInstruction(text.data, 0x4C, 0x8B, 0x05, kRdataRva);
    appendRipInstruction(text.data, 0x48, 0x8D, 0x15, kRdataRva + 16);
    appendRipInstruction(text.data, 0x48, 0x8D, 0x85, kRdataRva);
    appendRipInstruction(text.data, 0x48, 0x03, 0x05, kRdataRva);
    evr::test::appendBytes(text.data, {0xC3});

    evr::test::PeSpec spec;
    spec.sections = {text, rdata};
    fixture.bytes = evr::test::buildPe(spec);
    return fixture;
}

} // namespace

TEST_CASE("finds lea and mov references to a target and nothing else") {
    const Fixture fixture = buildFixture();
    const auto image = PeImage::parse(fixture.bytes);
    REQUIRE(image.ok());

    const auto refs = evr::resolver::findRipReferences(*image, *image->findSection(".text"), kRdataRva);
    REQUIRE(refs.size() == 2);
    CHECK(refs[0].instructionRva == fixture.leaRva);
    CHECK(refs[0].kind == RipInstruction::Lea);
    CHECK(refs[1].instructionRva == fixture.movRva);
    CHECK(refs[1].kind == RipInstruction::Mov);
}

TEST_CASE("anchor string leads to the code that uses it") {
    // The resolver's intended flow: find the string, then the instructions referencing it.
    const Fixture fixture = buildFixture();
    const auto image = PeImage::parse(fixture.bytes);
    REQUIRE(image.ok());

    const auto strings = evr::resolver::findStringInSection(*image, StringQuery::anchor("anchor string"));
    REQUIRE(strings.size() == 1);
    const auto refs = evr::resolver::findRipReferences(*image, *image->findSection(".text"), strings[0]);
    CHECK(refs.size() == 2);
}

TEST_CASE("displacements pointing backwards resolve correctly") {
    // Code placed after the data it references needs a negative displacement.
    evr::test::SectionSpec rdata{".rdata", 0x1000, 0, {}};
    evr::test::appendAscii(rdata.data, "early");

    constexpr std::uint32_t kLateTextRva = 0x4000;
    evr::test::SectionSpec text{".text", kLateTextRva, 0, {}};
    const std::uint32_t nextRva = kLateTextRva + evr::resolver::kRipInstructionLength;
    evr::test::appendBytes(text.data, {0x48, 0x8D, 0x05});
    evr::test::appendLe32(text.data, 0x1000u - nextRva);

    evr::test::PeSpec spec;
    spec.sections = {rdata, text};
    const auto bytes = evr::test::buildPe(spec);
    const auto image = PeImage::parse(bytes);
    REQUIRE(image.ok());

    const auto refs = evr::resolver::findRipReferences(*image, *image->findSection(".text"), 0x1000);
    REQUIRE(refs.size() == 1);
    CHECK(refs[0].instructionRva == kLateTextRva);
}

TEST_CASE("a section shorter than one instruction has no references") {
    evr::test::SectionSpec text{".text", kTextRva, 0, {0x48, 0x8D, 0x05}};
    evr::test::PeSpec spec;
    spec.sections = {text};
    const auto bytes = evr::test::buildPe(spec);
    const auto image = PeImage::parse(bytes);
    REQUIRE(image.ok());

    // The file-backed size is the 3 data bytes; the zero padding after them is not code.
    CHECK(evr::resolver::findRipReferences(*image, *image->findSection(".text"), 0).empty());
}
