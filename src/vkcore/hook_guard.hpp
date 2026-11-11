#pragma once

// A C++ exception must not leave a function the game or the Vulkan loader calls into: a layer entry point, a
// detour of a game function, a hook callback, a thread the layer starts. Neither is built to unwind one, so
// it ends the game. A boundary whose own work can throw (memory for a list or a string, a thread) runs that
// work through a HookGuard; when it fails, the boundary goes on without it: the next layer or the game's own
// function still runs with the game's arguments, the frame's extra work is skipped, or the feature stays off.
// mid_hook.cpp guards every mid hook callback the same way, in its thunk.

#include "common/finishes.hpp"
#include "vkcore/log.hpp"

#include <atomic>
#include <cstdint>
#include <utility>

namespace evr::vkcore {

class HookGuard {
public:
    // `where` names the boundary in the log (a string literal).
    constexpr explicit HookGuard(const char* where) noexcept : where_(where) {}

    // Runs `body`: true when it returned, false when it threw a std::exception. Each call site counts its own
    // failures (every lambda is a type of its own, with its own count): the first at a site is logged with
    // its message, then every 1000th with the count. Any thread.
    template <class Body>
    bool run(Body&& body) noexcept {
        static std::atomic<std::uint64_t> failures{0};
        return finishes(std::forward<Body>(body), [this](const char* what) { failed(failures, what); });
    }

    // Whether a site's `n`th failure is logged.
    static constexpr bool logged(std::uint64_t n) noexcept { return n == 1 || n % 1000 == 0; }

private:
    void failed(std::atomic<std::uint64_t>& failures, const char* what) const noexcept {
        const std::uint64_t n = failures.fetch_add(1, std::memory_order_relaxed) + 1;
        if (logged(n)) {
            EVR_LOG("guard: %s threw (%s); the layer's part was skipped (%llu time(s) so far at this site)",
                    where_, what, static_cast<unsigned long long>(n));
        }
    }

    const char* where_;
};

} // namespace evr::vkcore
