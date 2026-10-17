#include "engine/eternal/resolver/pattern.hpp"

#include <cstring>

#include <algorithm>
#include <array>
#include <cstdint>
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

// How common each byte value is in x86-64 game code: occurrences per 100,000 bytes of the game's .text
// (Steam build 25216728; the Game Pass build's counts are within a sixth of these). findAll searches for
// the pattern's least common fixed byte: most signatures start with 48 (REX.W, 7.8% of the code), and
// checking every 48 made each scan of the 43 MB .text take about 36 ms (Release) or 134 ms (Debug).
constexpr std::array<std::uint16_t, 256> kCodeByteFrequency = {
    9770, 1975, 885, 896,  691,  700,  314, 229, 944,  153,  165, 175,  352,  521,  108, 3753, // 00-0F
    1062, 419,  108, 140,  186,  312,  91,  84,  380,  87,   74,  85,   145,  85,   80,  218,  // 10-1F
    907,  67,   69,  81,   2508, 97,   58,  60,  924,  255,  62,  145,  132,  62,   74,  101,  // 20-2F
    542,  66,   86,  478,  99,   65,   48,  54,  460,  210,  56,  417,  152,  137,  61,  102,  // 30-3F
    759,  1578, 181, 355,  1506, 807,  150, 195, 7820, 951,  113, 188,  1394, 464,  109, 133,  // 40-4F
    482,  88,   75,  174,  291,  254,  153, 318, 427,  402,  66,  196,  515,  169,  152, 267,  // 50-5F
    294,  46,   92,  236,  121,  76,   436, 51,  190,  64,   37,  73,   197,  63,   72,  107,  // 60-6F
    260,  56,   94,  103,  1130, 536,  91,  110, 244,  78,   52,  101,  322,  203,  165, 163,  // 70-7F
    425,  304,  72,  1367, 578,  1000, 118, 176, 321,  2240, 56,  5004, 141,  1906, 120, 107,  // 80-8F
    323,  62,   65,  71,   93,   84,   52,  72,  153,  68,   39,  42,   111,  51,   54,  57,   // 90-9F
    174,  41,   35,  42,   76,   49,   38,  52,  160,  41,   54,  51,   87,   42,   39,  68,   // A0-AF
    199,  61,   44,  57,   114,  57,   244, 141, 302,  136,  193, 98,   113,  67,   62,  102,  // B0-BF
    1034, 512,  264, 613,  545,  265,  368, 648, 506,  365,  136, 273,  2880, 107,  214, 254,  // C0-CF
    332,  136,  254, 148,  82,   84,   122, 134, 271,  167,  110, 173,  89,   61,   67,  82,   // D0-DF
    258,  127,  111, 104,  96,   55,   54,  70,  1528, 307,  81,  333,  326,  99,   88,  91,   // E0-EF
    272,  128,  222, 972,  85,   73,   199, 118, 362,  194,  133, 81,   156,  109,  437, 2654, // F0-FF
};

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
    std::pair<std::size_t, std::byte> best{0, std::byte{0}};
    unsigned bestFrequency = UINT32_MAX;
    for (std::size_t i = 0; i < bytes_.size(); ++i) {
        if (!bytes_[i]) {
            continue;
        }
        const unsigned frequency = kCodeByteFrequency[std::to_integer<std::size_t>(*bytes_[i])];
        if (frequency < bestFrequency) {
            best = {i, *bytes_[i]};
            bestFrequency = frequency;
        }
    }
    return best;
}

std::vector<std::size_t> findAll(ByteSpan haystack, const Pattern& pattern) {
    // Candidates are the places where the anchor (the least common fixed byte) occurs (memchr), each
    // checked in full. Every match has the anchor at its offset + anchorAt, so no match is missed, and the
    // offsets come out in ascending order. The layer scans the game's 43 MB of code for some 200
    // signatures while the game starts, so the anchor's rarity sets how long VR waits.
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
