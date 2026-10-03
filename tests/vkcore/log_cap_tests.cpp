#include "vkcore/log.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using evr::vkcore::LogCap;

TEST_CASE("log cap: the first lines all pass, then one a minute with the count left out") {
    LogCap cap(3);
    std::uint64_t skipped = 99;
    CHECK(cap.due(1000, skipped));
    CHECK(skipped == 0);
    CHECK(cap.due(1001, skipped));
    CHECK(cap.due(1002, skipped));
    CHECK_FALSE(cap.due(1003, skipped));
    CHECK_FALSE(cap.due(30'000, skipped));
    CHECK_FALSE(cap.due(61'001, skipped));
    CHECK(cap.due(61'002, skipped));
    CHECK(skipped == 3);
    CHECK_FALSE(cap.due(61'003, skipped));
    CHECK(cap.due(200'000, skipped));
    CHECK(skipped == 1);
}

TEST_CASE("log cap: the interval can be set") {
    LogCap cap(0, 10);
    std::uint64_t skipped = 0;
    CHECK(cap.due(10, skipped));
    CHECK_FALSE(cap.due(15, skipped));
    CHECK(cap.due(20, skipped));
    CHECK(skipped == 1);
}
