#pragma once

// Byte signatures in the common IDA style: "48 8B ?? 05 ?? ?? ?? ??".
//
// Signatures are the resolver's last resort (ARCHITECTURE section 5): anchors and type info come
// first. Tokens are separated by whitespace; each is two hex digits or a wildcard written as "?" or
// "??". Hex digits may be upper or lower case.

#include "common/result.hpp"
#include "engine/eternal/resolver/byte_reader.hpp"
#include "engine/eternal/resolver/pe_image.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace evr::resolver {

enum class PatternError : std::uint8_t {
    Empty,        // No tokens at all.
    InvalidToken, // A token that is neither two hex digits nor a wildcard.
    AllWildcards, // Would match at every offset, which is never what the author meant.
};

class Pattern {
public:
    static Result<Pattern, PatternError> parse(std::string_view text);

    [[nodiscard]] std::size_t size() const { return bytes_.size(); }

    // True if the pattern matches `haystack` starting at `offset`. Out-of-range positions do not match.
    [[nodiscard]] bool matchesAt(ByteSpan haystack, std::size_t offset) const;

    // The first byte that is not a wildcard and its position (parse rejects all-wildcard patterns).
    [[nodiscard]] std::pair<std::size_t, std::byte> anchor() const;

private:
    explicit Pattern(std::vector<std::optional<std::byte>> bytes) : bytes_(std::move(bytes)) {}

    // nullopt is a wildcard.
    std::vector<std::optional<std::byte>> bytes_;
};

// All match offsets in `haystack`, in ascending order. Matches may overlap.
std::vector<std::size_t> findAll(ByteSpan haystack, const Pattern& pattern);

// All match RVAs within the file-backed bytes of one section.
std::vector<std::uint32_t> scanSection(const PeImage& image, const Section& section, const Pattern& pattern);

} // namespace evr::resolver
