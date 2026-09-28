#include "engine/eternal/resolver/pattern.hpp"

#include <cstring>

#include <algorithm>
#include <string>
#include <utility>

namespace evr::resolver {

namespace {

std::optional<std::uint8_t> hexDigitValue(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<std::uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<std::uint8_t>(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<std::uint8_t>(c - 'A' + 10);
    }
    return std::nullopt;
}

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Splits on runs of whitespace.
std::vector<std::string_view> tokenize(std::string_view text) {
    std::vector<std::string_view> tokens;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && isSpace(text[pos])) {
            ++pos;
        }
        const std::size_t start = pos;
        while (pos < text.size() && !isSpace(text[pos])) {
            ++pos;
        }
        if (pos > start) {
            tokens.push_back(text.substr(start, pos - start));
        }
    }
    return tokens;
}

} // namespace

Result<Pattern, PatternError> Pattern::parse(std::string_view text) {
    const std::vector<std::string_view> tokens = tokenize(text);
    if (tokens.empty()) {
        return fail(PatternError::Empty, "pattern has no tokens");
    }

    std::vector<std::optional<std::byte>> bytes;
    bytes.reserve(tokens.size());
    for (const std::string_view token : tokens) {
        if (token == "?" || token == "??") {
            bytes.emplace_back(std::nullopt);
            continue;
        }
        const bool isTwoChars = token.size() == 2;
        const auto high = isTwoChars ? hexDigitValue(token[0]) : std::nullopt;
        const auto low = isTwoChars ? hexDigitValue(token[1]) : std::nullopt;
        if (!high || !low) {
            return fail(PatternError::InvalidToken, "invalid pattern token '" + std::string(token) + "'");
        }
        bytes.emplace_back(static_cast<std::byte>((*high << 4) | *low));
    }

    const bool allWildcards = std::none_of(bytes.begin(), bytes.end(),
                                           [](const std::optional<std::byte>& b) { return b.has_value(); });
    if (allWildcards) {
        return fail(PatternError::AllWildcards, "pattern contains only wildcards");
    }
    return Pattern(std::move(bytes));
}

bool Pattern::matchesAt(ByteSpan haystack, std::size_t offset) const {
    if (!rangeFits(haystack.size(), offset, bytes_.size())) {
        return false;
    }
    for (std::size_t i = 0; i < bytes_.size(); ++i) {
        const std::optional<std::byte>& expected = bytes_[i];
        if (expected.has_value() && *expected != haystack[offset + i]) {
            return false;
        }
    }
    return true;
}

std::pair<std::size_t, std::byte> Pattern::anchor() const {
    for (std::size_t i = 0; i < bytes_.size(); ++i) {
        if (bytes_[i]) {
            return {i, *bytes_[i]};
        }
    }
    return {0, std::byte{0}};
}

std::vector<std::size_t> findAll(ByteSpan haystack, const Pattern& pattern) {
    // Candidates are the places where the first fixed byte occurs (memchr), each checked in full. The
    // layer scans the game's 40 MB of code for a dozen signatures while the game starts, so this matters.
    std::vector<std::size_t> matches;
    if (pattern.size() == 0 || pattern.size() > haystack.size()) {
        return matches;
    }
    const std::size_t lastStart = haystack.size() - pattern.size();
    const auto [anchorAt, anchorByte] = pattern.anchor();
    const auto* data = reinterpret_cast<const unsigned char*>(haystack.data());
    const int wanted = std::to_integer<int>(anchorByte);
    std::size_t from = anchorAt; // search position of the anchor byte
    const std::size_t end = lastStart + anchorAt + 1;
    while (from < end) {
        const void* hit = std::memchr(data + from, wanted, end - from);
        if (!hit) {
            break;
        }
        const auto at = static_cast<std::size_t>(static_cast<const unsigned char*>(hit) - data);
        const std::size_t offset = at - anchorAt;
        if (pattern.matchesAt(haystack, offset)) {
            matches.push_back(offset);
        }
        from = at + 1;
    }
    return matches;
}

std::vector<std::uint32_t> scanSection(const PeImage& image, const Section& section, const Pattern& pattern) {
    std::vector<std::uint32_t> rvas;
    for (const std::size_t offset : findAll(image.sectionBytes(section), pattern)) {
        // Parsing guarantees every section's addresses fit in 32 bits, but a Section built by hand
        // might not; such a match has no RVA and is skipped rather than wrapped around.
        const std::uint64_t rva = std::uint64_t{section.virtualAddress} + offset;
        if (rva <= UINT32_MAX) {
            rvas.push_back(static_cast<std::uint32_t>(rva));
        }
    }
    return rvas;
}

} // namespace evr::resolver
