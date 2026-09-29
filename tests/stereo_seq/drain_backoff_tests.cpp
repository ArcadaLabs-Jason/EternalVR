#include "stereo_seq/drain_backoff.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using evr::stereo_seq::DrainBackoff;
using evr::stereo_seq::drainRetryMs;
using evr::stereo_seq::kDrainRetryMaxMs;
using evr::stereo_seq::kDrainRetryMs;

TEST_CASE("drain backoff: each failure in a row doubles the wait up to the cap") {
    CHECK(drainRetryMs(0) == kDrainRetryMs);
    CHECK(drainRetryMs(1) == 2000);
    CHECK(drainRetryMs(2) == 4000);
    CHECK(drainRetryMs(3) == 8000);
    CHECK(drainRetryMs(4) == 16000);
    CHECK(drainRetryMs(5) == kDrainRetryMaxMs);
    CHECK(drainRetryMs(UINT32_MAX) == kDrainRetryMaxMs); // never overflows
}

TEST_CASE("drain backoff: a drain that succeeds starts over") {
    DrainBackoff backoff;
    CHECK(backoff.failed() == 2000);
    CHECK(backoff.failed() == 4000);
    CHECK(backoff.failed() == 8000);
    CHECK(backoff.failures() == 3);
    backoff.succeeded();
    CHECK(backoff.failures() == 0);
    CHECK(backoff.failed() == 2000); // the first failure after a success waits the base time again
}

TEST_CASE("drain backoff: the wait stays at the cap however long the failures go on") {
    DrainBackoff backoff;
    std::uint64_t wait = 0;
    for (int i = 0; i < 100; ++i) {
        wait = backoff.failed();
    }
    CHECK(wait == kDrainRetryMaxMs);
    CHECK(backoff.failures() == 100);
}
