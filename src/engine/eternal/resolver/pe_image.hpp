#pragma once

// Read-only view of a PE32+ executable as it exists on disk (a FILE image, not a loaded one).
//
// The resolver works on file images so that it can run anywhere: in the game process on the exe read
// from disk, and in CI against archived executables on any OS (ARCHITECTURE section 5). Only the
// parts the resolver needs are parsed: the DOS and NT headers and the section table.
//
// The view does not own the bytes. The buffer passed to parse() must outlive the PeImage.

#include "common/result.hpp"
#include "engine/eternal/resolver/byte_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::resolver {

enum class PeError : std::uint8_t {
    Truncated,          // A header or table extends past the end of the buffer.
    BadDosSignature,    // No "MZ" at offset 0.
    BadNtSignature,     // No "PE\0\0" at e_lfanew.
    UnsupportedMachine, // Not an x86-64 image.
    NotPe32Plus,        // Optional header is not the 64-bit (PE32+) format.
    SectionOutOfBounds, // A section's raw data lies outside the buffer.
    // Headers and sections that cannot all be mapped consistently: SizeOfHeaders reaching into the
    // first section, sections overlapping in memory or in the file, or a section extending past the
    // 4 GiB address space. The loader would refuse such an image, and address conversions on it would
    // be ambiguous.
    InconsistentLayout,
};

struct Section {
    std::string name;
    std::uint32_t virtualAddress = 0;
    std::uint32_t virtualSize = 0;
    std::uint32_t rawOffset = 0;
    std::uint32_t rawSize = 0;
    std::uint32_t characteristics = 0;

    // Number of bytes of this section actually present in the file. Raw data is padded to the file
    // alignment, and the virtual tail beyond the raw data is zero-filled only at load time.
    [[nodiscard]] std::uint32_t fileBackedSize() const;

    // Bytes of address space the section occupies once loaded.
    [[nodiscard]] std::uint32_t virtualExtent() const;

    [[nodiscard]] bool containsRva(std::uint32_t rva) const;
};

class PeImage {
public:
    static Result<PeImage, PeError> parse(ByteSpan bytes);

    [[nodiscard]] ByteSpan bytes() const { return bytes_; }
    [[nodiscard]] std::uint64_t imageBase() const { return imageBase_; }
    [[nodiscard]] std::uint32_t sizeOfHeaders() const { return sizeOfHeaders_; }
    [[nodiscard]] const std::vector<Section>& sections() const { return sections_; }

    // Exact name match, e.g. ".text". Returns nullptr when absent.
    [[nodiscard]] const Section* findSection(std::string_view name) const;

    // Maps an RVA to a file offset. Fails for RVAs outside every section's file-backed range (for
    // example .bss data, which has no bytes on disk).
    [[nodiscard]] std::optional<std::size_t> rvaToOffset(std::uint32_t rva) const;
    [[nodiscard]] std::optional<std::uint32_t> offsetToRva(std::size_t offset) const;

    // The file-backed bytes of a section. Empty for a section whose bytes are not inside this image,
    // which only happens for a Section that did not come from sections().
    [[nodiscard]] ByteSpan sectionBytes(const Section& section) const;

    // `size` bytes starting at `rva`, if they are all file-backed and in one section (or headers).
    [[nodiscard]] std::optional<ByteSpan> bytesAtRva(std::uint32_t rva, std::size_t size) const;

private:
    PeImage(ByteSpan bytes,
            std::uint64_t imageBase,
            std::uint32_t sizeOfHeaders,
            std::vector<Section> sections);

    ByteSpan bytes_;
    std::uint64_t imageBase_ = 0;
    std::uint32_t sizeOfHeaders_ = 0;
    std::vector<Section> sections_;
};

} // namespace evr::resolver
