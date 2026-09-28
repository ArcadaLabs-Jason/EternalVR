#include "engine/eternal/resolver/string_search.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>

namespace evr::resolver {

namespace {

// The exact byte sequence to look for, including the terminator when one is required.
std::vector<std::byte> encodeNeedle(const StringQuery& query) {
    std::vector<std::byte> needle;
    const bool wide = query.encoding == StringEncoding::Utf16Le;

    auto append = [&](char c) {
        needle.push_back(static_cast<std::byte>(c));
        if (wide) {
            needle.push_back(std::byte{0});
        }
    };

    for (const char c : query.text) {
        append(c);
    }
    if (query.match == StringMatch::Terminated) {
        append('\0');
    }
    return needle;
}

} // namespace

std::vector<std::uint32_t>
findString(const PeImage& image, const Section& section, const StringQuery& query) {
    std::vector<std::uint32_t> rvas;
    if (query.text.empty()) {
        return rvas;
    }

    const std::vector<std::byte> needle = encodeNeedle(query);
    const ByteSpan haystack = image.sectionBytes(section);
    const std::boyer_moore_horspool_searcher searcher(needle.begin(), needle.end());
    // UTF-16 strings are stored on 2-byte boundaries. A match at an odd offset straddles two code
    // units, e.g. the high byte of one character and the low byte of the next, and is not the text.
    const bool wide = query.encoding == StringEncoding::Utf16Le;

    // Restart one byte past each hit so overlapping occurrences are all reported.
    for (auto it = std::search(haystack.begin(), haystack.end(), searcher); it != haystack.end();
         it = std::search(std::next(it), haystack.end(), searcher)) {
        const auto offset = static_cast<std::size_t>(it - haystack.begin());
        if (wide && offset % 2 != 0) {
            continue;
        }
        // Parsing guarantees every section's addresses fit in 32 bits, but a Section built by hand
        // might not; such a hit has no RVA and is skipped rather than wrapped around.
        const std::uint64_t rva = std::uint64_t{section.virtualAddress} + offset;
        if (rva <= UINT32_MAX) {
            rvas.push_back(static_cast<std::uint32_t>(rva));
        }
    }
    return rvas;
}

std::vector<std::uint32_t>
findStringInSection(const PeImage& image, const StringQuery& query, std::string_view sectionName) {
    const Section* section = image.findSection(sectionName);
    if (section == nullptr) {
        return {};
    }
    return findString(image, *section, query);
}

} // namespace evr::resolver
