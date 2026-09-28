#include "engine/eternal/resolver/pe_image.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace evr::resolver {

namespace {

// Field offsets from the PE/COFF specification. Only the fields we read are listed.
namespace layout {
constexpr std::uint16_t kDosMagic = 0x5A4D; // "MZ"
constexpr std::size_t kDosLfanewOffset = 0x3C;

constexpr std::uint32_t kNtSignature = 0x00004550; // "PE\0\0"
constexpr std::size_t kNtSignatureSize = 4;

// COFF file header, relative to its start (just after the NT signature).
constexpr std::size_t kFileHeaderSize = 20;
constexpr std::size_t kMachineOffset = 0;
constexpr std::size_t kNumberOfSectionsOffset = 2;
constexpr std::size_t kSizeOfOptionalHeaderOffset = 16;
constexpr std::uint16_t kMachineAmd64 = 0x8664;

// Optional header, relative to its start.
constexpr std::uint16_t kPe32PlusMagic = 0x20B;
constexpr std::size_t kMagicOffset = 0;
constexpr std::size_t kImageBaseOffset = 24;
constexpr std::size_t kSizeOfHeadersOffset = 60;

// Section header entries.
constexpr std::size_t kSectionHeaderSize = 40;
constexpr std::size_t kSectionNameSize = 8;
constexpr std::size_t kVirtualSizeOffset = 8;
constexpr std::size_t kVirtualAddressOffset = 12;
constexpr std::size_t kSizeOfRawDataOffset = 16;
constexpr std::size_t kPointerToRawDataOffset = 20;
constexpr std::size_t kCharacteristicsOffset = 36;
} // namespace layout

using ParseResult = Result<PeImage, PeError>;

std::string truncatedMessage(std::string_view what, std::size_t offset) {
    return std::string(what) + " at file offset " + std::to_string(offset) + " extends past end of image";
}

// Section names are up to 8 bytes, NUL-padded, and not NUL-terminated when all 8 are used.
std::string readSectionName(ByteSpan entry) {
    std::string name;
    for (std::size_t i = 0; i < layout::kSectionNameSize; ++i) {
        const char c = static_cast<char>(std::to_integer<unsigned char>(entry[i]));
        if (c == '\0') {
            break;
        }
        name.push_back(c);
    }
    return name;
}

Section readSectionHeader(ByteSpan entry) {
    // The caller has bounds-checked the whole entry, so these reads cannot fail.
    Section section;
    section.name = readSectionName(entry);
    section.virtualSize = readLe<std::uint32_t>(entry, layout::kVirtualSizeOffset).value_or(0);
    section.virtualAddress = readLe<std::uint32_t>(entry, layout::kVirtualAddressOffset).value_or(0);
    section.rawSize = readLe<std::uint32_t>(entry, layout::kSizeOfRawDataOffset).value_or(0);
    section.rawOffset = readLe<std::uint32_t>(entry, layout::kPointerToRawDataOffset).value_or(0);
    section.characteristics = readLe<std::uint32_t>(entry, layout::kCharacteristicsOffset).value_or(0);
    return section;
}

// True when the half-open ranges [aStart, aStart + aSize) and [bStart, bStart + bSize) share a byte. Empty
// ranges overlap nothing. Sizes are 64-bit so the ends cannot wrap.
bool rangesOverlap(std::uint64_t aStart, std::uint64_t aSize, std::uint64_t bStart, std::uint64_t bSize) {
    return aSize != 0 && bSize != 0 && aStart < bStart + bSize && bStart < aStart + aSize;
}

// Checks that headers and sections map to memory and to the file one-to-one. The address
// conversions rely on this: with overlaps, one RVA or file offset would belong to two places, and
// the first match would silently win.
std::optional<Error<PeError>>
checkLayout(std::size_t fileSize, std::uint32_t sizeOfHeaders, const std::vector<Section>& sections) {
    if (sizeOfHeaders > fileSize) {
        return fail(PeError::Truncated,
                    "SizeOfHeaders (" + std::to_string(sizeOfHeaders) + ") extends past end of image");
    }
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const Section& a = sections[i];
        if (std::uint64_t{a.virtualAddress} + a.virtualExtent() > std::uint64_t{UINT32_MAX} + 1) {
            return fail(PeError::InconsistentLayout,
                        "section '" + a.name + "' extends past the 32-bit address space");
        }
        const bool overlapsHeaders = rangesOverlap(0, sizeOfHeaders, a.virtualAddress, a.virtualExtent()) ||
                                     rangesOverlap(0, sizeOfHeaders, a.rawOffset, a.fileBackedSize());
        if (overlapsHeaders) {
            const std::string headers = "SizeOfHeaders " + std::to_string(sizeOfHeaders);
            return fail(PeError::InconsistentLayout,
                        "section '" + a.name + "' overlaps the headers (" + headers + ")");
        }
        for (std::size_t j = i + 1; j < sections.size(); ++j) {
            const Section& b = sections[j];
            if (rangesOverlap(a.virtualAddress, a.virtualExtent(), b.virtualAddress, b.virtualExtent())) {
                return fail(PeError::InconsistentLayout,
                            "sections '" + a.name + "' and '" + b.name + "' overlap in memory");
            }
            if (rangesOverlap(a.rawOffset, a.fileBackedSize(), b.rawOffset, b.fileBackedSize())) {
                return fail(PeError::InconsistentLayout,
                            "sections '" + a.name + "' and '" + b.name + "' overlap in the file");
            }
        }
    }
    return std::nullopt;
}

} // namespace

std::uint32_t Section::fileBackedSize() const {
    if (virtualSize == 0) {
        return rawSize;
    }
    return std::min(rawSize, virtualSize);
}

std::uint32_t Section::virtualExtent() const {
    return (virtualSize != 0) ? virtualSize : rawSize;
}

bool Section::containsRva(std::uint32_t rva) const {
    return rva >= virtualAddress && rva - virtualAddress < virtualExtent();
}

PeImage::PeImage(ByteSpan bytes,
                 std::uint64_t imageBase,
                 std::uint32_t sizeOfHeaders,
                 std::vector<Section> sections)
    : bytes_(bytes), imageBase_(imageBase), sizeOfHeaders_(sizeOfHeaders), sections_(std::move(sections)) {}

Result<PeImage, PeError> PeImage::parse(ByteSpan bytes) {
    const auto dosMagic = readLe<std::uint16_t>(bytes, 0);
    if (!dosMagic) {
        return fail(PeError::Truncated, "image too small for a DOS header");
    }
    if (*dosMagic != layout::kDosMagic) {
        return fail(PeError::BadDosSignature, "missing MZ signature");
    }

    const auto lfanew = readLe<std::uint32_t>(bytes, layout::kDosLfanewOffset);
    if (!lfanew) {
        return fail(PeError::Truncated, "image too small for e_lfanew");
    }
    const std::size_t ntOffset = *lfanew;

    const auto ntSignature = readLe<std::uint32_t>(bytes, ntOffset);
    if (!ntSignature) {
        return fail(PeError::Truncated, truncatedMessage("NT signature", ntOffset));
    }
    if (*ntSignature != layout::kNtSignature) {
        return fail(PeError::BadNtSignature, "missing PE signature at e_lfanew");
    }

    const std::size_t fileHeaderOffset = ntOffset + layout::kNtSignatureSize;
    if (!rangeFits(bytes.size(), fileHeaderOffset, layout::kFileHeaderSize)) {
        return fail(PeError::Truncated, truncatedMessage("COFF file header", fileHeaderOffset));
    }
    const auto fileHeader = bytes.subspan(fileHeaderOffset, layout::kFileHeaderSize);
    const auto machine = readLe<std::uint16_t>(fileHeader, layout::kMachineOffset).value_or(0);
    const auto sectionCount = readLe<std::uint16_t>(fileHeader, layout::kNumberOfSectionsOffset).value_or(0);
    const auto optionalHeaderSize =
        readLe<std::uint16_t>(fileHeader, layout::kSizeOfOptionalHeaderOffset).value_or(0);

    if (machine != layout::kMachineAmd64) {
        return fail(PeError::UnsupportedMachine, "machine type " + std::to_string(machine) + " is not AMD64");
    }

    const std::size_t optionalHeaderOffset = fileHeaderOffset + layout::kFileHeaderSize;
    if (!rangeFits(bytes.size(), optionalHeaderOffset, optionalHeaderSize) ||
        optionalHeaderSize < layout::kSizeOfHeadersOffset + sizeof(std::uint32_t)) {
        return fail(PeError::Truncated, truncatedMessage("optional header", optionalHeaderOffset));
    }
    const auto optionalHeader = bytes.subspan(optionalHeaderOffset, optionalHeaderSize);
    if (readLe<std::uint16_t>(optionalHeader, layout::kMagicOffset) != layout::kPe32PlusMagic) {
        return fail(PeError::NotPe32Plus, "optional header magic is not PE32+");
    }
    const auto imageBase = readLe<std::uint64_t>(optionalHeader, layout::kImageBaseOffset).value_or(0);
    const auto sizeOfHeaders =
        readLe<std::uint32_t>(optionalHeader, layout::kSizeOfHeadersOffset).value_or(0);

    const std::size_t sectionTableOffset = optionalHeaderOffset + optionalHeaderSize;
    const std::size_t sectionTableSize = std::size_t{sectionCount} * layout::kSectionHeaderSize;
    if (!rangeFits(bytes.size(), sectionTableOffset, sectionTableSize)) {
        return fail(PeError::Truncated, truncatedMessage("section table", sectionTableOffset));
    }

    std::vector<Section> sections;
    sections.reserve(sectionCount);
    for (std::size_t i = 0; i < sectionCount; ++i) {
        const auto entry =
            bytes.subspan(sectionTableOffset + i * layout::kSectionHeaderSize, layout::kSectionHeaderSize);
        Section section = readSectionHeader(entry);
        if (!rangeFits(bytes.size(), section.rawOffset, section.fileBackedSize())) {
            return fail(PeError::SectionOutOfBounds,
                        "raw data of section '" + section.name + "' lies outside the image");
        }
        sections.push_back(std::move(section));
    }

    if (auto layoutError = checkLayout(bytes.size(), sizeOfHeaders, sections)) {
        return std::move(*layoutError);
    }
    return PeImage(bytes, imageBase, sizeOfHeaders, std::move(sections));
}

const Section* PeImage::findSection(std::string_view name) const {
    const auto it = std::find_if(sections_.begin(), sections_.end(),
                                 [name](const Section& section) { return section.name == name; });
    return (it != sections_.end()) ? &*it : nullptr;
}

std::optional<std::size_t> PeImage::rvaToOffset(std::uint32_t rva) const {
    // Headers are mapped at RVA 0 with identical file offsets.
    if (rva < sizeOfHeaders_) {
        if (rva >= bytes_.size()) {
            return std::nullopt;
        }
        return rva;
    }
    for (const Section& section : sections_) {
        if (rva < section.virtualAddress) {
            continue;
        }
        const std::uint32_t delta = rva - section.virtualAddress;
        if (delta < section.fileBackedSize()) {
            return std::size_t{section.rawOffset} + delta;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> PeImage::offsetToRva(std::size_t offset) const {
    if (offset >= bytes_.size()) {
        return std::nullopt;
    }
    if (offset < sizeOfHeaders_) {
        return static_cast<std::uint32_t>(offset);
    }
    for (const Section& section : sections_) {
        if (offset < section.rawOffset) {
            continue;
        }
        const std::size_t delta = offset - section.rawOffset;
        if (delta < section.fileBackedSize()) {
            // Cannot wrap: parse() checked that the section's addresses fit in 32 bits.
            return static_cast<std::uint32_t>(section.virtualAddress + delta);
        }
    }
    return std::nullopt;
}

ByteSpan PeImage::sectionBytes(const Section& section) const {
    // Always true for this image's own sections, which parse() checked.
    if (!rangeFits(bytes_.size(), section.rawOffset, section.fileBackedSize())) {
        return {};
    }
    return bytes_.subspan(section.rawOffset, section.fileBackedSize());
}

std::optional<ByteSpan> PeImage::bytesAtRva(std::uint32_t rva, std::size_t size) const {
    const std::optional<std::size_t> start = rvaToOffset(rva);
    if (!start) {
        return std::nullopt;
    }
    // The last byte must map too, and contiguously; checking it through rvaToOffset keeps a range
    // from silently running off the end of one section into unrelated file data.
    if (size > 0) {
        const std::uint64_t lastRva = std::uint64_t{rva} + size - 1;
        if (lastRva > UINT32_MAX) {
            return std::nullopt;
        }
        const std::optional<std::size_t> last = rvaToOffset(static_cast<std::uint32_t>(lastRva));
        if (!last || *last != *start + size - 1) {
            return std::nullopt;
        }
    }
    return bytes_.subspan(*start, size);
}

} // namespace evr::resolver
