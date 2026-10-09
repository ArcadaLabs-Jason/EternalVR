#include "common/xr_recovery.hpp"

#include <doctest/doctest.h>

#include <string>

using evr::xr_recovery::Loss;

TEST_CASE("a lost session or runtime is recovered from; the runtime's exit request is not") {
    CHECK(evr::xr_recovery::recovers(Loss::Session));
    CHECK(evr::xr_recovery::recovers(Loss::Instance));
    CHECK_FALSE(evr::xr_recovery::recovers(Loss::Exiting));
}

TEST_CASE("reconnect attempts back off to every 5 seconds") {
    CHECK(evr::xr_recovery::retryDelayMs(0) == 1000);
    CHECK(evr::xr_recovery::retryDelayMs(1) == 2000);
    CHECK(evr::xr_recovery::retryDelayMs(4) == 5000);
    CHECK(evr::xr_recovery::retryDelayMs(5) == 5000);
    CHECK(evr::xr_recovery::retryDelayMs(1000) == 5000);
}

TEST_CASE("the first attempts are logged, then one a minute") {
    int logged = 0;
    for (std::uint32_t attempt = 0; attempt < 5 + 12 * 10; ++attempt) {
        logged += evr::xr_recovery::logsAttempt(attempt) ? 1 : 0;
    }
    CHECK(logged == 5 + 10);
    CHECK(evr::xr_recovery::logsAttempt(5));
    CHECK_FALSE(evr::xr_recovery::logsAttempt(6));
    CHECK(evr::xr_recovery::logsAttempt(17));
}

TEST_CASE("a failed xrBeginSession is tried again, then the session is made again, then VR stays off") {
    using evr::xr_recovery::afterBeginFailure;
    using evr::xr_recovery::BeginNext;
    using evr::xr_recovery::kBeginRestarts;
    using evr::xr_recovery::kBeginTries;
    for (std::uint32_t failures = 1; failures < kBeginTries; ++failures) {
        CHECK(afterBeginFailure(failures, 0) == BeginNext::Retry);
        CHECK(afterBeginFailure(failures, kBeginRestarts) == BeginNext::Retry);
    }
    CHECK(afterBeginFailure(kBeginTries, 0) == BeginNext::Restart);
    CHECK(afterBeginFailure(kBeginTries, kBeginRestarts - 1) == BeginNext::Restart);
    CHECK(afterBeginFailure(kBeginTries, kBeginRestarts) == BeginNext::GiveUp);
    CHECK(afterBeginFailure(kBeginTries + 5, kBeginRestarts + 1) == BeginNext::GiveUp);
}

TEST_CASE("a loss from xrBeginSession makes the session again at once, within the same budget") {
    using evr::xr_recovery::afterBeginFailure;
    using evr::xr_recovery::BeginNext;
    using evr::xr_recovery::kBeginRestarts;
    CHECK(afterBeginFailure(1, 0, true) == BeginNext::Restart);
    CHECK(afterBeginFailure(1, kBeginRestarts - 1, true) == BeginNext::Restart);
    CHECK(afterBeginFailure(1, kBeginRestarts, true) == BeginNext::GiveUp);
    // A runtime that loses every session as it begins: each new session's first call is a loss.
    std::uint32_t restarts = 0;
    int cycles = 0;
    while (afterBeginFailure(1, restarts, true) == BeginNext::Restart && cycles < 100) {
        ++restarts;
        ++cycles;
    }
    CHECK(cycles == static_cast<int>(kBeginRestarts));
}

TEST_CASE("each loss has a name for the log") {
    CHECK(std::string(evr::xr_recovery::lossName(Loss::Session)) == "session lost");
    CHECK(std::string(evr::xr_recovery::lossName(Loss::Instance)) == "runtime lost");
    CHECK(std::string(evr::xr_recovery::lossName(Loss::Exiting)) == "exiting");
}
