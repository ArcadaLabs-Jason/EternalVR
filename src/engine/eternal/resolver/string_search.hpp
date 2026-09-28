#pragma once

// Finds string anchors (ARCHITECTURE section 5, step 1).
//
// Long-lived assert and log strings survive game patches. Locating one in .rdata and then finding
// the code that references it (see rip_xref.hpp) leads to the function that uses it.

#include "engine/eternal/resolver/pe_image.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace evr::resolver {

enum class StringEncoding : std::uint8_t {
    Ascii,
    // Each character of the ASCII search text becomes one 16-bit little-endian code unit. Non-ASCII
    // search text is not supported; engine anchors are plain ASCII. Only matches at an even offset
    // from the section start count, since that is where UTF-16 code units begin.
    Utf16Le,
};

enum class StringMatch : std::uint8_t {
    // The occurrence must be followed by a NUL terminator, so "idPlayer" does not match inside
    // "idPlayerHud". This is the normal mode for anchors.
    Terminated,
    // Any occurrence, including as a prefix or inside a longer string.
    Anywhere,
};

struct StringQuery {
    std::string_view text;
    StringEncoding encoding = StringEncoding::Ascii;
    StringMatch match = StringMatch::Terminated;

    // The usual anchor lookup: an exact, NUL-terminated ASCII string.
    static constexpr StringQuery anchor(std::string_view text) {
        return {text, StringEncoding::Ascii, StringMatch::Terminated};
    }
};

// RVAs of every occurrence of the query in one section, in ascending order.
//
// A Terminated match only checks what follows the text. The linker may merge identical string tails,
// so a match can be the tail of a longer string; callers that care should check the preceding byte.
std::vector<std::uint32_t> findString(const PeImage& image, const Section& section, const StringQuery& query);

// Searches the section named `sectionName` (".rdata" by default). Returns nothing if it is absent.
std::vector<std::uint32_t>
findStringInSection(const PeImage& image, const StringQuery& query, std::string_view sectionName = ".rdata");

} // namespace evr::resolver
