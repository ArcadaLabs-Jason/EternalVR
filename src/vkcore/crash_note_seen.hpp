#pragma once

// Which exceptions the crash note logs (crash_note.hpp). An exception is seen first-chance, before anything
// handles it, so most are not crashes: the layer's own guarded reads and the game's take a fault and carry
// on. The note logs the first kFirst at any place, then only places it has not logged yet (up to kPlaces),
// and counts the rest. A fault the game dies of is then in the log as its last exception line unless it
// struck at a place already logged, which then names the place all the same.
//
// Pure: no Windows; the caller serialises calls.

#include <array>
#include <cstddef>
#include <cstdint>

namespace evr::vkcore {

// The exception codes worth a line: the ones a game dies of. Not C++ exceptions (0xE06D7363), breakpoints or
// the debugger's codes.
constexpr bool crashClassCode(std::uint32_t code) {
    switch (code) {
    case 0xC0000005u: // access violation
    case 0xC000001Du: // illegal instruction
    case 0xC0000096u: // privileged instruction
    case 0xC00000FDu: // stack overflow
    case 0xC0000094u: // integer divide by zero
    case 0xC000008Cu: // array bounds exceeded
    case 0xC0000006u: // in-page error
    case 0xC0000374u: // heap corruption
    case 0xC0000409u: // stack buffer overrun / fail fast
        return true;
    default:
        return false;
    }
}

class CrashSeen {
public:
    static constexpr std::size_t kFirst = 16;
    static constexpr std::size_t kPlaces = 64;

    // A crash-class exception at `place` (the faulting instruction's address): whether to log it. `repeats`
    // gets how many were not logged since the last line.
    bool note(std::uint64_t place, std::uint64_t& repeats) {
        ++total_;
        repeats = 0;
        bool known = false;
        for (std::size_t i = 0; i < count_; ++i) {
            if (places_[i] == place) {
                known = true;
                break;
            }
        }
        if (!known && count_ < kPlaces) {
            places_[count_++] = place;
        }
        if (total_ <= kFirst || !known) {
            repeats = skipped_;
            skipped_ = 0;
            return true;
        }
        ++skipped_;
        return false;
    }

    [[nodiscard]] std::uint64_t total() const { return total_; }

private:
    std::array<std::uint64_t, kPlaces> places_{};
    std::size_t count_ = 0;
    std::uint64_t total_ = 0;
    std::uint64_t skipped_ = 0;
};

} // namespace evr::vkcore
