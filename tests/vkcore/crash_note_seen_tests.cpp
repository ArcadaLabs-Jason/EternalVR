#include "vkcore/crash_note_seen.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using evr::vkcore::crashClassCode;
using evr::vkcore::CrashSeen;

TEST_CASE("the crash note takes the faults a game dies of, not C++ exceptions or breakpoints") {
    CHECK(crashClassCode(0xC0000005u));       // access violation
    CHECK(crashClassCode(0xC00000FDu));       // stack overflow
    CHECK(crashClassCode(0xC0000409u));       // fail fast
    CHECK_FALSE(crashClassCode(0xE06D7363u)); // a C++ throw
    CHECK_FALSE(crashClassCode(0x80000003u)); // a breakpoint
    CHECK_FALSE(crashClassCode(0x80000001u)); // a guard page (stack growth)
    CHECK_FALSE(crashClassCode(0x406D1388u)); // a thread's name for the debugger
}

TEST_CASE("after the first ones only new places are logged, so the fatal fault is not crowded out") {
    CrashSeen seen;
    std::uint64_t repeats = 0;
    // A guarded read that faults over and over at one place (handled every time).
    for (std::size_t i = 0; i < CrashSeen::kFirst; ++i) {
        CHECK(seen.note(0x1000, repeats));
    }
    for (int i = 0; i < 1000; ++i) {
        CHECK_FALSE(seen.note(0x1000, repeats));
    }
    // The fault the game dies of, somewhere new: logged, with the count of the ones held back.
    CHECK(seen.note(0x2000, repeats));
    CHECK(repeats == 1000);
    CHECK(seen.total() == CrashSeen::kFirst + 1001);
    // The same place again is held back now.
    CHECK_FALSE(seen.note(0x2000, repeats));
}

TEST_CASE("past the place book's size, a new place is still logged (it is only not remembered)") {
    CrashSeen seen;
    std::uint64_t repeats = 0;
    for (std::uint64_t p = 1; p <= CrashSeen::kPlaces; ++p) {
        CHECK(seen.note(p, repeats));
    }
    CHECK(seen.note(CrashSeen::kPlaces + 100, repeats));
    CHECK(seen.note(CrashSeen::kPlaces + 100, repeats)); // not remembered: logged again
}
