#include "engine/eternal/sun_keep_registers.hpp"

#include <doctest/doctest.h>

using evr::engine::sunKeepRegisters;

TEST_CASE("the registers a kept slice hands back") {
    const auto k = sunKeepRegisters(0xAAAAAAAA00000000ull, 0x1234567800000006ull, 1);
    CHECK(k.rax == 0xAAAAAAAA00000001ull); // eax = the counter: `cmp edx, eax` finds no excess
    CHECK(k.rbx == 0x1234567800000000ull); // bits 2 and 4 cleared, the rest kept
    const auto j = sunKeepRegisters(0x00000000FFFFFFFFull, 0xFFull, 3);
    CHECK(j.rax == 3);
    CHECK(j.rbx == 0xF9ull);
}
