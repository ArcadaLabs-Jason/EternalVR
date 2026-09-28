#include "engine/eternal/resolver/rip_xref.hpp"

#include "engine/eternal/resolver/byte_reader.hpp"

#include <optional>

namespace evr::resolver {

namespace {

constexpr std::uint8_t kRexW = 0x48;
constexpr std::uint8_t kRexWR = 0x4C;
constexpr std::uint8_t kOpcodeLea = 0x8D;
constexpr std::uint8_t kOpcodeMov = 0x8B;

// ModRM with mod = 00 and rm = 101 selects [rip + disp32]; the reg field (bits 3-5) is free.
constexpr std::uint8_t kModAndRmMask = 0xC7;
constexpr std::uint8_t kModRmRipRelative = 0x05;

constexpr std::size_t kDisplacementOffset = 3;

std::uint8_t byteAt(ByteSpan bytes, std::size_t offset) {
    return std::to_integer<std::uint8_t>(bytes[offset]);
}

// Decodes the instruction kind if `offset` starts a recognised encoding.
std::optional<RipInstruction> matchEncoding(ByteSpan code, std::size_t offset) {
    const std::uint8_t rex = byteAt(code, offset);
    if (rex != kRexW && rex != kRexWR) {
        return std::nullopt;
    }
    if ((byteAt(code, offset + 2) & kModAndRmMask) != kModRmRipRelative) {
        return std::nullopt;
    }
    switch (byteAt(code, offset + 1)) {
    case kOpcodeLea:
        return RipInstruction::Lea;
    case kOpcodeMov:
        return RipInstruction::Mov;
    default:
        return std::nullopt;
    }
}

} // namespace

std::vector<RipReference>
findRipReferences(const PeImage& image, const Section& codeSection, std::uint32_t targetRva) {
    std::vector<RipReference> references;
    const ByteSpan code = image.sectionBytes(codeSection);
    if (code.size() < kRipInstructionLength) {
        return references;
    }

    const std::size_t lastStart = code.size() - kRipInstructionLength;
    for (std::size_t offset = 0; offset <= lastStart; ++offset) {
        const std::optional<RipInstruction> kind = matchEncoding(code, offset);
        if (!kind) {
            continue;
        }

        // Signed 64-bit arithmetic so that a displacement pointing below RVA 0 or above 4 GiB is
        // simply a non-match rather than a wrapped-around false hit.
        const std::int64_t instructionRva =
            std::int64_t{codeSection.virtualAddress} + static_cast<std::int64_t>(offset);
        const std::int64_t displacement = readLeI32(code, offset + kDisplacementOffset).value_or(0);
        const std::int64_t resolved = instructionRva + kRipInstructionLength + displacement;

        if (resolved == std::int64_t{targetRva}) {
            references.push_back({static_cast<std::uint32_t>(instructionRva), *kind});
        }
    }
    return references;
}

} // namespace evr::resolver
