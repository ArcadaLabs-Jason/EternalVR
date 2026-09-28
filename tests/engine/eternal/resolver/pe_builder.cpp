#include "pe_builder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace evr::test {

namespace {

constexpr std::uint32_t kSectionAlignment = 0x1000;
constexpr std::uint16_t kOptionalHeaderSize = 240; // Standard PE32+ size with 16 data directories.
constexpr std::size_t kSectionHeaderSize = 40;

std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

std::uint32_t virtualSizeOf(const SectionSpec& section) {
    return section.virtualSize != 0 ? section.virtualSize : static_cast<std::uint32_t>(section.data.size());
}

// Little-endian writes into a pre-sized buffer.
class Writer {
public:
    explicit Writer(std::vector<std::byte>& out) : out_(out) {}

    void u8(std::size_t offset, std::uint8_t value) { out_[offset] = static_cast<std::byte>(value); }

    void u16(std::size_t offset, std::uint16_t value) {
        for (std::size_t i = 0; i < 2; ++i) {
            u8(offset + i, static_cast<std::uint8_t>(value >> (8 * i)));
        }
    }

    void u32(std::size_t offset, std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) {
            u8(offset + i, static_cast<std::uint8_t>(value >> (8 * i)));
        }
    }

    void u64(std::size_t offset, std::uint64_t value) {
        for (std::size_t i = 0; i < 8; ++i) {
            u8(offset + i, static_cast<std::uint8_t>(value >> (8 * i)));
        }
    }

private:
    std::vector<std::byte>& out_;
};

void writeHeaders(Writer& w, const PeSpec& spec) {
    // DOS header: "MZ" and e_lfanew.
    w.u16(0, 0x5A4D);
    w.u32(0x3C, kPeHeaderOffset);

    // NT signature "PE\0\0" followed by the COFF file header.
    w.u32(kPeHeaderOffset, 0x00004550);
    const std::size_t fileHeader = kPeHeaderOffset + 4;
    w.u16(fileHeader + 0, spec.machine);
    w.u16(fileHeader + 2, static_cast<std::uint16_t>(spec.sections.size()));
    w.u16(fileHeader + 16, kOptionalHeaderSize);

    // Optional header: magic, image base, alignments, size of image and headers.
    const std::size_t optionalHeader = fileHeader + 20;
    std::uint32_t sizeOfImage = kSizeOfHeaders;
    for (const SectionSpec& section : spec.sections) {
        sizeOfImage = std::max(sizeOfImage,
                               alignUp(section.virtualAddress + virtualSizeOf(section), kSectionAlignment));
    }
    w.u16(optionalHeader + 0, spec.optionalHeaderMagic);
    w.u64(optionalHeader + 24, spec.imageBase);
    w.u32(optionalHeader + 32, kSectionAlignment);
    w.u32(optionalHeader + 36, kFileAlignment);
    w.u32(optionalHeader + 56, sizeOfImage);
    w.u32(optionalHeader + 60, kSizeOfHeaders);
}

void writeSectionHeader(Writer& w,
                        std::size_t entryOffset,
                        const SectionSpec& section,
                        std::uint32_t rawOffset) {
    for (std::size_t i = 0; i < std::min<std::size_t>(section.name.size(), 8); ++i) {
        w.u8(entryOffset + i, static_cast<std::uint8_t>(section.name[i]));
    }
    w.u32(entryOffset + 8, virtualSizeOf(section));
    w.u32(entryOffset + 12, section.virtualAddress);
    w.u32(entryOffset + 16, alignUp(static_cast<std::uint32_t>(section.data.size()), kFileAlignment));
    w.u32(entryOffset + 20, rawOffset);
}

} // namespace

std::uint32_t sectionFileOffset(const PeSpec& spec, std::size_t index) {
    std::uint32_t offset = kSizeOfHeaders;
    for (std::size_t i = 0; i < index; ++i) {
        offset += alignUp(static_cast<std::uint32_t>(spec.sections[i].data.size()), kFileAlignment);
    }
    return offset;
}

std::vector<std::byte> buildPe(const PeSpec& spec) {
    const std::uint32_t fileSize = sectionFileOffset(spec, spec.sections.size());
    std::vector<std::byte> image(fileSize, std::byte{0});
    Writer w(image);

    writeHeaders(w, spec);

    const std::size_t sectionTable = kPeHeaderOffset + 4 + 20 + kOptionalHeaderSize;
    for (std::size_t i = 0; i < spec.sections.size(); ++i) {
        const SectionSpec& section = spec.sections[i];
        const std::uint32_t rawOffset = sectionFileOffset(spec, i);
        writeSectionHeader(w, sectionTable + i * kSectionHeaderSize, section, rawOffset);
        for (std::size_t j = 0; j < section.data.size(); ++j) {
            w.u8(rawOffset + j, section.data[j]);
        }
    }
    return image;
}

void appendBytes(std::vector<std::uint8_t>& out, std::initializer_list<std::uint8_t> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

void appendLe32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

void appendAscii(std::vector<std::uint8_t>& out, const std::string& text, bool terminate) {
    for (const char c : text) {
        out.push_back(static_cast<std::uint8_t>(c));
    }
    if (terminate) {
        out.push_back(0);
    }
}

void appendUtf16Le(std::vector<std::uint8_t>& out, const std::string& text, bool terminate) {
    for (const char c : text) {
        out.push_back(static_cast<std::uint8_t>(c));
        out.push_back(0);
    }
    if (terminate) {
        out.push_back(0);
        out.push_back(0);
    }
}

} // namespace evr::test
