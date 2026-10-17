#include "vkcore/game_text.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// findLeaReferences and callReturns (game_text.hpp): pure scans over a GameImage's .text, in their own
// library so the tests can run them on a synthetic image.

namespace evr::vkcore {

namespace {

std::int32_t rel32(const std::byte* at) {
    std::int32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

} // namespace

std::vector<const std::byte*> findLeaReferences(const GameImage& image, const std::byte* target) {
    // Candidates are the 8D opcode bytes (memchr), each checked for the prefix before it and the ModRM
    // after it: a check at every byte of the 43 MB .text took 60 ms per string (Release).
    std::vector<const std::byte*> hits;
    const std::byte* text = image.text.data();
    const std::size_t n = image.text.size();
    if (n < 7) {
        return hits;
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(text);
    const auto wanted = reinterpret_cast<std::intptr_t>(target);
    const std::size_t end = n - 5; // the opcode of an instruction at i (i + 7 <= n) is at i + 1 < n - 5
    for (std::size_t from = 1; from < end;) {
        const void* hit = std::memchr(bytes + from, 0x8D, end - from);
        if (!hit) {
            break;
        }
        const auto opcode = static_cast<std::size_t>(static_cast<const unsigned char*>(hit) - bytes);
        const std::size_t i = opcode - 1;
        if ((bytes[i] == 0x48 || bytes[i] == 0x4C) && (bytes[opcode + 1] & 0xC7u) == 0x05u &&
            reinterpret_cast<std::intptr_t>(text + i + 7) + rel32(text + i + 3) == wanted) {
            hits.push_back(text + i);
        }
        from = opcode + 1;
    }
    return hits;
}

std::vector<std::uintptr_t>
callReturns(const GameImage& image, const std::byte* function, const std::byte* target, std::size_t span) {
    // Every E8 (memchr) from `function` while the whole call lies within `span` and .text.
    std::vector<std::uintptr_t> found;
    if (span < 5 || !image.inText(function, 5)) {
        return found;
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(image.text.data());
    const auto start = static_cast<std::size_t>(function - image.text.data());
    const std::size_t textLast = image.text.size() - 5; // the last call start within .text
    const std::size_t end = 1 + (span - 5 <= textLast - start ? start + span - 5 : textLast);
    for (std::size_t from = start; from < end;) {
        const void* hit = std::memchr(bytes + from, 0xE8, end - from);
        if (!hit) {
            break;
        }
        const auto offset = static_cast<std::size_t>(static_cast<const unsigned char*>(hit) - bytes);
        const std::byte* at = image.text.data() + offset;
        if (at + 5 + rel32(at + 1) == target) {
            found.push_back(reinterpret_cast<std::uintptr_t>(at + 5));
        }
        from = offset + 1;
    }
    return found;
}

} // namespace evr::vkcore
