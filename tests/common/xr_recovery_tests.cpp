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

TEST_CASE("each loss has a name for the log") {
    CHECK(std::string(evr::xr_recovery::lossName(Loss::Session)) == "session lost");
    CHECK(std::string(evr::xr_recovery::lossName(Loss::Instance)) == "runtime lost");
    CHECK(std::string(evr::xr_recovery::lossName(Loss::Exiting)) == "exiting");
}
