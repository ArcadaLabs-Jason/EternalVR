#pragma once

// For work whose exception must not escape: one leaving a hook or a detached thread ends the game. The
// optional work a capture does (an image's memory, its writer thread) is skipped instead.

#include <exception>
#include <utility>

namespace evr {

// Runs `body`: true when it returned, false when it threw a std::exception (out of memory, no thread).
template <class Body>
bool finishes(Body&& body) noexcept {
    try {
        std::forward<Body>(body)();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace evr
