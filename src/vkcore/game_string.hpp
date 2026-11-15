#pragma once

// A NUL-terminated string in game memory (a map's name, a decl's name), copied without reading past the
// page its NUL is on. A fixed-size copy of a short string runs on into the bytes after it, and when the
// string sits near the end of its allocation the next page may not be mapped: the copy faults, the guard
// handles it, and the crash note logs a first-chance access violation for a pointer that was good (two
// bench-combat runs failed QA on that at the map load). Copied a page at a time, a read faults only when
// a page the string itself lies on cannot be read.

#include <cstddef>
#include <cstdint>

namespace evr::vkcore {

inline constexpr std::size_t kGameStringPage = 4096; // the smallest unit Windows maps and protects

// How many bytes to copy from `address` before the next page starts, at most `wanted`.
constexpr std::size_t pageChunk(std::uintptr_t address, std::size_t wanted) {
    const std::size_t toPage = kGameStringPage - static_cast<std::size_t>(address % kGameStringPage);
    return wanted < toPage ? wanted : toPage;
}

struct GameString {
    std::size_t length = 0; // characters copied, not counting the NUL written after them
    bool complete = false;  // the string's own NUL was read (false: cut at `capacity`, or a page unreadable)
};

// Copies the string at `source` into `out` (`capacity` bytes, always NUL-terminated when capacity > 0).
// Never reads beyond the page holding the string's NUL, nor beyond `capacity - 1` bytes. A page that
// cannot be read ends the copy there (an access violation handled, as for any guarded read).
[[nodiscard]] GameString copyGameString(char* out, std::size_t capacity, const void* source);

} // namespace evr::vkcore
