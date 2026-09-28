#include "gpu_timing/query_ring.hpp"
#include "gpu_timing/timestamps.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <vector>

using evr::gpu_timing::QueryRing;
using evr::gpu_timing::signedTicks;
using evr::gpu_timing::ticksToMs;
using evr::gpu_timing::validMask;

TEST_CASE("the valid mask covers exactly the valid bits") {
    CHECK(validMask(0) == 0);
    CHECK(validMask(1) == 1);
    CHECK(validMask(36) == 0xF'FFFF'FFFFull);
    CHECK(validMask(63) == 0x7FFF'FFFF'FFFF'FFFFull);
    CHECK(validMask(64) == ~std::uint64_t{0});
    CHECK(validMask(70) == ~std::uint64_t{0});
}

TEST_CASE("timestamp differences are right without a wrap") {
    CHECK(signedTicks(1000, 1500, 64) == 500);
    CHECK(signedTicks(1500, 1000, 64) == -500);
    CHECK(signedTicks(1000, 1500, 36) == 500);
    CHECK(signedTicks(1500, 1000, 36) == -500);
    CHECK(signedTicks(7, 7, 48) == 0);
}

TEST_CASE("timestamp differences are right across the counter's wrap at the valid bits") {
    const std::uint64_t top = validMask(36); // the last value before the 36-bit counter wraps to 0
    CHECK(signedTicks(top - 99, 100, 36) == 200);
    CHECK(signedTicks(100, top - 99, 36) == -200);
    // Bits above the valid ones (never written by a conforming driver) are ignored.
    CHECK(signedTicks(top - 99, (std::uint64_t{1} << 40) | 100, 36) == 200);
    // A 64-bit counter wraps the same way.
    CHECK(signedTicks(~std::uint64_t{0} - 9, 10, 64) == 20);
    CHECK(signedTicks(10, ~std::uint64_t{0} - 9, 64) == -20);
    // Small counters: half the range forward, the rest backward.
    CHECK(signedTicks(0, 7, 4) == 7);
    CHECK(signedTicks(0, 8, 4) == -8);
    CHECK(signedTicks(14, 2, 4) == 4);
}

TEST_CASE("no valid bits means no timestamps") {
    CHECK(signedTicks(100, 5000, 0) == 0);
}

TEST_CASE("ticks convert to milliseconds by the timestamp period") {
    CHECK(ticksToMs(1'000'000.0, 1.0f) == doctest::Approx(1.0));
    CHECK(ticksToMs(1'000.0, 41.667f) == doctest::Approx(0.041667).epsilon(1e-4));
    CHECK(ticksToMs(0.0, 10.0f) == 0.0);
}

TEST_CASE("query pairs are handed out in order and own two adjacent queries") {
    QueryRing ring(4);
    CHECK(ring.pairs() == 4);
    CHECK(ring.queryCount() == 8);
    std::vector<std::uint32_t> got;
    for (int i = 0; i < 4; ++i) {
        const std::optional<std::uint32_t> p = ring.acquire();
        REQUIRE(p.has_value());
        got.push_back(*p);
    }
    CHECK(got == std::vector<std::uint32_t>{0, 1, 2, 3});
    CHECK(ring.inUse() == 4);
    CHECK(QueryRing::beginQuery(3) == 6);
    CHECK(QueryRing::endQuery(3) == 7);
}

TEST_CASE("a full ring times nothing until the next pair in order comes back") {
    QueryRing ring(3);
    REQUIRE(ring.acquire() == 0u);
    REQUIRE(ring.acquire() == 1u);
    REQUIRE(ring.acquire() == 2u);
    CHECK_FALSE(ring.acquire().has_value());
    // Pair 1 coming back does not help: pair 0 is next and still busy (no skipping).
    ring.release(1);
    CHECK_FALSE(ring.acquire().has_value());
    ring.release(0);
    CHECK(ring.acquire() == 0u);
    CHECK(ring.acquire() == 1u);
    CHECK_FALSE(ring.acquire().has_value());
    CHECK(ring.inUse() == 3);
}

TEST_CASE("the ring wraps around indefinitely when pairs come back") {
    QueryRing ring(5);
    for (std::uint32_t i = 0; i < 1000; ++i) {
        const std::optional<std::uint32_t> p = ring.acquire();
        REQUIRE(p.has_value());
        CHECK(*p == i % 5);
        ring.release(*p);
    }
    CHECK(ring.inUse() == 0);
}

TEST_CASE("releasing a pair twice or out of range changes nothing") {
    QueryRing ring(2);
    REQUIRE(ring.acquire() == 0u);
    ring.release(0);
    ring.release(0);
    ring.release(9);
    CHECK(ring.inUse() == 0);
    CHECK(ring.acquire() == 1u);
    CHECK(ring.inUse() == 1);
}
