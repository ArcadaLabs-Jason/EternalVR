#pragma once

// Assembles small synthetic PE32+ file images for resolver tests.
//
// The images are minimal but structurally real: a DOS header, NT headers with a PE32+ optional
// header, a section table, and section data at file-aligned offsets. Knobs exist to corrupt specific
// fields so each parser error path can be exercised.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace evr::test {

struct SectionSpec {
    std::string name;
    std::uint32_t virtualAddress = 0;
    // Zero means "same as the data size".
    std::uint32_t virtualSize = 0;
    std::vector<std::uint8_t> data;
};

struct PeSpec {
    std::uint64_t imageBase = 0x140000000;
    std::uint16_t machine = 0x8664;
    std::uint16_t optionalHeaderMagic = 0x20B;
    std::vector<SectionSpec> sections;
};

// Fixed layout choices, exposed so tests can reason about offsets.
inline constexpr std::uint32_t kPeHeaderOffset = 0x80;
inline constexpr std::uint32_t kFileAlignment = 0x200;
inline constexpr std::uint32_t kSizeOfHeaders = 0x400;

std::vector<std::byte> buildPe(const PeSpec& spec);

// File offset at which section `index` of an image built from `spec` starts.
std::uint32_t sectionFileOffset(const PeSpec& spec, std::size_t index);

// Helpers for writing section contents.
void appendBytes(std::vector<std::uint8_t>& out, std::initializer_list<std::uint8_t> bytes);
void appendLe32(std::vector<std::uint8_t>& out, std::uint32_t value);
void appendAscii(std::vector<std::uint8_t>& out, const std::string& text, bool terminate = true);
void appendUtf16Le(std::vector<std::uint8_t>& out, const std::string& text, bool terminate = true);

} // namespace evr::test
