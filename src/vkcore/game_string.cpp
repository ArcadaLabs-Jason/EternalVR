#include "vkcore/game_string.hpp"

#include "vkcore/seh_filter.hpp"

#include <windows.h>

#include <cstring>

namespace evr::vkcore {

namespace {

bool copyGuarded(void* destination, const void* source, std::size_t size) {
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

} // namespace

GameString copyGameString(char* out, std::size_t capacity, const void* source) {
    GameString result;
    if (!out || capacity == 0) {
        return result;
    }
    out[0] = '\0';
    if (!source) {
        return result;
    }
    const auto* from = static_cast<const char*>(source);
    const auto start = reinterpret_cast<std::uintptr_t>(source);
    while (result.length + 1 < capacity) {
        // Pages are mapped and protected whole: each chunk is all readable or not at all.
        const std::size_t chunk = pageChunk(start + result.length, capacity - 1 - result.length);
        if (!copyGuarded(out + result.length, from + result.length, chunk)) {
            break;
        }
        if (const void* nul = std::memchr(out + result.length, 0, chunk)) {
            result.length = static_cast<std::size_t>(static_cast<const char*>(nul) - out);
            result.complete = true;
            return result;
        }
        result.length += chunk;
    }
    out[result.length] = '\0';
    return result;
}

} // namespace evr::vkcore
