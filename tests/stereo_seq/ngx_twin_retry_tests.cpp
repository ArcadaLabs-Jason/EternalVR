#include "stereo_seq/ngx_twin_retry.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using evr::stereo_seq::kNgxTwinRetries;
using evr::stereo_seq::kNgxTwinRetryMaxMs;
using evr::stereo_seq::kNgxTwinRetryMs;
using evr::stereo_seq::NgxTwinRetry;
using evr::stereo_seq::ngxTwinRetryMs;

TEST_CASE("NGX twin retry: the wait doubles per failure up to the cap") {
    CHECK(ngxTwinRetryMs(0) == kNgxTwinRetryMs);
    CHECK(ngxTwinRetryMs(1) == kNgxTwinRetryMs);
    CHECK(ngxTwinRetryMs(2) == 2 * kNgxTwinRetryMs);
    CHECK(ngxTwinRetryMs(3) == 4 * kNgxTwinRetryMs);
    CHECK(ngxTwinRetryMs(4) == kNgxTwinRetryMaxMs);
    CHECK(ngxTwinRetryMs(UINT32_MAX) == kNgxTwinRetryMaxMs);
}

TEST_CASE("NGX twin retry: no fallback until a failure") {
    NgxTwinRetry r;
    CHECK_FALSE(r.fallback());
    CHECK_FALSE(r.due(1'000'000));
    CHECK_FALSE(r.requested());
    r.released();
    CHECK_FALSE(r.due(1'000'000));
    CHECK_FALSE(r.created()); // the first twin: not a recovery
}

TEST_CASE("NGX twin retry: a failure falls back, then a try after the wait, then recovery") {
    NgxTwinRetry r;
    r.failed(1000);
    CHECK(r.fallback());
    CHECK(r.failures() == 1);
    CHECK_FALSE(r.due(1000));
    CHECK_FALSE(r.due(1000 + kNgxTwinRetryMs - 1));
    CHECK(r.due(1000 + kNgxTwinRetryMs));
    CHECK_FALSE(r.fallback());                  // DLSS is held again for the try
    CHECK_FALSE(r.due(1000 + kNgxTwinRetryMs)); // one try per wait
    CHECK(r.created());
    CHECK(r.failures() == 0);
    CHECK_FALSE(r.fallback());
    CHECK_FALSE(r.created()); // already recovered
}

TEST_CASE("NGX twin retry: the game's other feature failing in the same try counts once") {
    NgxTwinRetry r;
    r.failed(0);
    r.failed(10);
    CHECK(r.failures() == 1);
    CHECK(r.due(kNgxTwinRetryMs)); // the wait runs from the first failure
}

TEST_CASE("NGX twin retry: the tries run out, then only the player starts them over") {
    NgxTwinRetry r;
    std::uint64_t now = 0;
    r.failed(now);
    for (std::uint32_t i = 1; i <= kNgxTwinRetries; ++i) {
        CHECK_FALSE(r.exhausted());
        now += ngxTwinRetryMs(i);
        CHECK(r.due(now));
        r.failed(now);
        CHECK(r.failures() == i + 1);
    }
    CHECK(r.exhausted());
    CHECK(r.fallback());
    CHECK_FALSE(r.due(now + 3'600'000));
    r.released(); // a game release does not add tries
    CHECK_FALSE(r.due(now + 3'600'000));
    // The player chose DLSS in the menu: a try at once, the count started over.
    CHECK(r.requested());
    CHECK_FALSE(r.exhausted());
    CHECK(r.failures() == 0);
    CHECK(r.due(now + 3'600'000));
    r.failed(now + 3'600'000);
    CHECK(r.failures() == 1);
    CHECK_FALSE(r.exhausted());
}

TEST_CASE("NGX twin retry: a release of the failed feature ends the wait") {
    NgxTwinRetry r;
    r.failed(500);
    CHECK_FALSE(r.due(600));
    r.released();
    CHECK(r.due(600));
    CHECK(r.created());
}

TEST_CASE("NGX twin retry: a clock before the failure keeps waiting") {
    NgxTwinRetry r;
    r.failed(10'000);
    CHECK_FALSE(r.due(5'000));
    CHECK(r.due(10'000 + kNgxTwinRetryMs));
}
