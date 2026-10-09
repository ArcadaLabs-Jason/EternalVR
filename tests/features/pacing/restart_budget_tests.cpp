#include "features/pacing/restart_budget.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <ostream>

using evr::pacing::RestartBudget;
using Verdict = evr::pacing::RestartBudget::Verdict;

namespace {

constexpr double kFrame = 1.0 / 90.0;

// Healthy frames at 90 Hz from `from` for `seconds`; how many restarts they earned back.
int healthy(RestartBudget& budget, double from, double seconds) {
    int earned = 0;
    for (double t = from; t < from + seconds; t += kFrame) {
        earned += budget.onHealthyFrame(t) ? 1 : 0;
    }
    return earned;
}

} // namespace

TEST_CASE("three restarts are available, then none") {
    RestartBudget budget;
    CHECK(budget.available() == RestartBudget::kMaxRestarts);
    CHECK(budget.onStall(10.0) == Verdict::Restart);
    CHECK(budget.onStall(60.0) == Verdict::Restart);
    CHECK(budget.onStall(110.0) == Verdict::Restart);
    CHECK(budget.onStall(160.0) == Verdict::NoneLeft);
    CHECK(budget.taken() == 3);
}

TEST_CASE("a restart comes back after ten healthy minutes") {
    RestartBudget budget;
    CHECK(budget.onStall(0.0) == Verdict::Restart);
    CHECK(budget.onStall(50.0) == Verdict::Restart); // two of three within 100 s, as in the player logs
    CHECK(healthy(budget, 100.0, RestartBudget::kEarnSeconds - 1.0) == 0);
    CHECK(budget.available() == 1);
    CHECK(healthy(budget, 100.0 + RestartBudget::kEarnSeconds - 1.0, 2.0) == 1);
    CHECK(budget.available() == 2);
    // Up to the three it started with, no more.
    CHECK(healthy(budget, 800.0, 2 * RestartBudget::kEarnSeconds + 10.0) == 1);
    CHECK(budget.available() == RestartBudget::kMaxRestarts);
}

TEST_CASE("a stall starts the healthy time over, and gaps between frames do not count") {
    RestartBudget budget;
    budget.onStall(0.0);
    CHECK(healthy(budget, 10.0, 500.0) == 0);
    CHECK(budget.onStall(510.0) == Verdict::Restart); // the 500 s are lost
    CHECK(budget.healthySeconds() == doctest::Approx(0.0));
    CHECK(healthy(budget, 520.0, 300.0) == 0);
    // No frames for an hour (no session): that time does not count.
    CHECK(healthy(budget, 4000.0, 299.0) == 0);
    CHECK(healthy(budget, 4299.0, 2.0) == 1);
}

TEST_CASE("a stall with none left still starts the healthy time over") {
    RestartBudget budget;
    for (int i = 0; i < 3; ++i) {
        budget.onStall(i * 10.0);
    }
    CHECK(healthy(budget, 30.0, 400.0) == 0);
    CHECK(budget.onStall(430.0) == Verdict::NoneLeft);
    CHECK(healthy(budget, 440.0, 400.0) == 0); // 400 s since the stall, not 800
    CHECK(healthy(budget, 840.0, 201.0) == 1);
    CHECK(budget.onStall(1050.0) == Verdict::Restart);
}

TEST_CASE("never more than the hard limit within the window, whatever was earned") {
    RestartBudget budget;
    double t = 0.0;
    for (std::size_t i = 0; i < RestartBudget::kMaxPerWindow; ++i) {
        if (budget.available() == 0) {
            healthy(budget, t, RestartBudget::kEarnSeconds + 1.0);
            t += RestartBudget::kEarnSeconds + 1.0;
        }
        CHECK(budget.onStall(t) == Verdict::Restart);
        t += 1.0;
    }
    // A restart earned back is still refused while the window holds kMaxPerWindow restarts.
    healthy(budget, t, RestartBudget::kEarnSeconds + 1.0);
    t += RestartBudget::kEarnSeconds + 1.0;
    REQUIRE(budget.available() > 0);
    CHECK(t < RestartBudget::kWindowSeconds);
    CHECK(budget.recent(t) == RestartBudget::kMaxPerWindow);
    CHECK(budget.onStall(t) == Verdict::RateLimited);
    // Once the first of them is out of the window, one is allowed again.
    CHECK(budget.onStall(RestartBudget::kWindowSeconds + 0.5) == Verdict::Restart);
}
