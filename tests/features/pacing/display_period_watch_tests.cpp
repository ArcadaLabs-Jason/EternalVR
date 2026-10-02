#include "features/pacing/display_period_watch.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace evr::pacing;

namespace {

constexpr double k144 = 1000.0 / 144.0; // 6.94 ms
constexpr double k72 = 1000.0 / 72.0;   // 13.89 ms
constexpr double k48 = 1000.0 / 48.0;   // 20.83 ms
constexpr double k90 = 1000.0 / 90.0;   // 11.11 ms

// Feeds frames of `periodMs`, each one period after the last, from `clock` on; returns the changes.
std::vector<PeriodChange> run(DisplayPeriodWatch& watch, double& clock, double periodMs, int frames) {
    std::vector<PeriodChange> changes;
    for (int i = 0; i < frames; ++i) {
        if (const auto change = watch.onFrame(periodMs, clock)) {
            changes.push_back(*change);
        }
        clock += periodMs / 1000.0;
    }
    return changes;
}

double secondsAt(const DisplayPeriodWatch& watch, double periodMs) {
    for (const PeriodTime& t : watch.times()) {
        if (samePeriod(t.periodMs, periodMs)) {
            return t.seconds;
        }
    }
    return 0.0;
}

} // namespace

TEST_CASE("period watch: the first period settles after the hold frames, then nothing changes") {
    DisplayPeriodWatch watch;
    double clock = 10.0;
    const auto first = run(watch, clock, k90, DisplayPeriodWatch::kHoldFrames - 1);
    CHECK(first.empty());
    CHECK(watch.settledMs() == 0.0);
    const auto settled = run(watch, clock, k90, 1);
    REQUIRE(settled.size() == 1);
    CHECK(settled[0].fromMs == 0.0);
    CHECK(settled[0].toMs == doctest::Approx(k90));
    CHECK(settled[0].atSeconds == doctest::Approx(10.0));
    CHECK(settled[0].referenceMs == 0.0);
    CHECK(run(watch, clock, k90, 500).empty());
    CHECK(watch.changes() == 0);
}

TEST_CASE("period watch: a few odd frames are no change") {
    DisplayPeriodWatch watch;
    double clock = 0.0;
    run(watch, clock, k144, 100);
    for (int odd = 1; odd < DisplayPeriodWatch::kHoldFrames; ++odd) {
        CHECK(run(watch, clock, k72, odd).empty());
        CHECK(run(watch, clock, k144, 50).empty());
    }
    // Alternating odd frames never add up to a change either.
    for (int i = 0; i < 40; ++i) {
        CHECK(run(watch, clock, i % 2 ? k72 : k48, 1).empty());
    }
    CHECK(watch.settledMs() == doctest::Approx(k144));
    CHECK(watch.changes() == 0);
}

TEST_CASE("period watch: a measured period wandering a little is the same period") {
    DisplayPeriodWatch watch;
    double clock = 0.0;
    run(watch, clock, 6.94, 50);
    for (int i = 0; i < 200; ++i) {
        CHECK_FALSE(watch.onFrame(i % 3 == 0 ? 6.99 : 6.90, clock).has_value());
        clock += 0.007;
    }
    CHECK(watch.changes() == 0);
    CHECK(samePeriod(6.94, 7.10));
    CHECK_FALSE(samePeriod(k144, 1000.0 / 120.0));
    CHECK_FALSE(samePeriod(k90, 1000.0 / 80.0));
}

TEST_CASE("period watch: flips are changes, each with its time; base and time per period") {
    DisplayPeriodWatch watch;
    double clock = 100.0;
    run(watch, clock, k144, 144 * 5); // 5 s
    CHECK(watch.baseMs() == doctest::Approx(k144));
    const double flipAt = clock;
    const auto down = run(watch, clock, k72, 72 * 2); // 2 s at 2x
    REQUIRE(down.size() == 1);
    CHECK(down[0].fromMs == doctest::Approx(k144));
    CHECK(down[0].toMs == doctest::Approx(k72));
    CHECK(down[0].atSeconds == doctest::Approx(flipAt));
    CHECK(down[0].referenceMs == doctest::Approx(k144));
    const auto up = run(watch, clock, k144, 144);
    REQUIRE(up.size() == 1);
    CHECK(up[0].toMs == doctest::Approx(k144));
    CHECK(watch.changes() == 2);
    // The hold frames of each new period count for the one before.
    CHECK(secondsAt(watch, k144) == doctest::Approx(6.0).epsilon(0.05));
    CHECK(secondsAt(watch, k72) == doctest::Approx(2.0).epsilon(0.05));
    CHECK(watch.times().size() == 2);
}

TEST_CASE("period watch: the base is the shortest period that held, not one passing through") {
    DisplayPeriodWatch watch;
    double clock = 0.0;
    run(watch, clock, k72, 72 * 2); // started throttled
    CHECK(watch.baseMs() == 0.0);
    CHECK(watch.referenceMs() == doctest::Approx(k72)); // the shortest so far stands in until a base
    run(watch, clock, k72, 72 * 2);
    CHECK(watch.baseMs() == doctest::Approx(k72));
    run(watch, clock, k144, 144); // 1 s only
    CHECK(watch.baseMs() == doctest::Approx(k72));
    CHECK(watch.referenceMs() == doctest::Approx(k72)); // the base, once there is one
    run(watch, clock, k144, 144 * 3);
    CHECK(watch.baseMs() == doctest::Approx(k144));
    run(watch, clock, k72, 72 * 10); // a longer period never replaces it
    CHECK(watch.baseMs() == doctest::Approx(k144));
}

TEST_CASE("period watch: gaps are not counted, nor bad periods") {
    DisplayPeriodWatch watch;
    double clock = 0.0;
    run(watch, clock, k90, 90);
    const double before = secondsAt(watch, k90);
    clock += 30.0; // the session was not running
    run(watch, clock, k90, 1);
    CHECK(secondsAt(watch, k90) == doctest::Approx(before));
    CHECK_FALSE(watch.onFrame(0.0, clock).has_value());
    CHECK_FALSE(watch.onFrame(-5.0, clock).has_value());
    CHECK_FALSE(watch.onFrame(5000.0, clock).has_value());
    CHECK(watch.settledMs() == doctest::Approx(k90));
}

TEST_CASE("period watch: distinct periods are capped") {
    DisplayPeriodWatch watch;
    double clock = 0.0;
    double period = 5.0;
    for (int i = 0; i < 60; ++i) {
        run(watch, clock, period, 20);
        period *= 1.05;
    }
    CHECK(watch.times().size() == DisplayPeriodWatch::kMaxPeriods);
    CHECK(watch.changes() == 59);
}

TEST_CASE("multiples and the summary's base") {
    CHECK(multipleOf(k144, k144) == 1);
    CHECK(multipleOf(k72, k144) == 2);
    CHECK(multipleOf(k48, k144) == 3);
    CHECK(multipleOf(k90, k144) == 0); // 1.6
    CHECK(multipleOf(k144 * 0.5, k144) == 0);
    CHECK(multipleOf(k90, 0.0) == 0);
    CHECK(hertz(k90) == doctest::Approx(90.0));
    CHECK(hertz(0.0) == 0.0);
    CHECK(summaryBase(k144, 0.0) == doctest::Approx(k144));
    CHECK(summaryBase(0.0, k90) == doctest::Approx(k90));
    // A session throttled from its start: the runtime's refresh period is the base.
    CHECK(summaryBase(2.0 * k90, k90) == doctest::Approx(k90));
    // A runtime value that does not fit (SteamVR's stays at its start value) leaves the measured base.
    CHECK(summaryBase(k144, k90) == doctest::Approx(k144));
    CHECK(summaryBase(k90, k144) == doctest::Approx(k90));
}

TEST_CASE("change lines name throttling, a return, and refresh changes") {
    PeriodChange first{0.0, k90, 4.21, 0.0};
    CHECK((changeText(first, 0.0) == "display period 11.11 ms at 4.2 s (90 Hz)"));
    CHECK((changeText(first, k90) == "display period 11.11 ms at 4.2 s (90 Hz, the headset's refresh rate)"));
    PeriodChange throttled{0.0, 2.0 * k90, 4.2, 0.0};
    CHECK((changeText(throttled, k90) ==
           "display period 22.22 ms at 4.2 s (2x the headset's 11.11 ms at 90 Hz: "
           "the runtime is throttling or reprojecting)"));
    CHECK((changeText({0.0, k144, 1.0, 0.0}, k90) ==
           "display period 6.94 ms at 1.0 s (144 Hz; the runtime reports 90.0 Hz)"));

    PeriodChange down{k144, k72, 312.44, k144};
    CHECK((changeText(down, 0.0) == "display period 6.94 -> 13.89 ms at 312.4 s (2x the base 6.94 ms: the "
                                    "runtime is throttling or reprojecting)"));
    PeriodChange third{k72, k48, 313.0, k144};
    CHECK((changeText(third, 0.0) == "display period 13.89 -> 20.83 ms at 313.0 s (3x the base 6.94 ms: the "
                                     "runtime is throttling or reprojecting)"));
    PeriodChange back{k72, k144, 315.0, k144};
    CHECK((changeText(back, 0.0) == "display period 13.89 -> 6.94 ms at 315.0 s (back to the base: 144 Hz)"));
    CHECK((changeText(back, k144) ==
           "display period 13.89 -> 6.94 ms at 315.0 s (back to the headset's refresh rate: 144 Hz)"));
    PeriodChange refresh{k144, k90, 400.0, k144};
    CHECK((changeText(refresh, 0.0) ==
           "display period 6.94 -> 11.11 ms at 400.0 s (a refresh change to 90 Hz)"));
    // The runtime reads 72 Hz: a real refresh change, not throttling at 2x.
    PeriodChange to72{k144, k72, 401.0, k144};
    CHECK(
        (changeText(to72, k72) == "display period 6.94 -> 13.89 ms at 401.0 s (a refresh change to 72 Hz)"));
    PeriodChange returned{k90, k144, 500.0, k144};
    CHECK((changeText(returned, 0.0) ==
           "display period 11.11 -> 6.94 ms at 500.0 s (a refresh change to 144 Hz)"));
    PeriodChange faster{k72, k144, 20.0, k72};
    CHECK((changeText(faster, 0.0) ==
           "display period 13.89 -> 6.94 ms at 20.0 s (shorter than any steady period before: 144 Hz)"));
}

TEST_CASE("refresh summary: shares by multiple of the base, others by rate") {
    const std::vector<PeriodTime> times{{k144, 81.5}, {k72, 17.2}, {k48, 0.9}, {k90, 0.4}};
    const RefreshShares s = shares(times, k144);
    CHECK(s.totalSeconds == doctest::Approx(100.0));
    REQUIRE(s.multiples.size() == 3);
    CHECK(s.multiples[0] == doctest::Approx(0.815));
    CHECK(s.throttled() == doctest::Approx(0.181));
    CHECK(s.other == doctest::Approx(0.004));
    CHECK((summaryText(s, 12) == "base 6.94 ms (144 Hz); 1x 81.5%, 2x 17.2%, 3x 0.9%, other 0.4% (90 Hz "
                                 "0.4%); 12 change(s) in 100 s"));

    const RefreshShares steady = shares({{k90, 60.0}}, k90);
    CHECK(steady.throttled() == 0.0);
    CHECK((summaryText(steady, 0) == "base 11.11 ms (90 Hz); 1x 100.0%; 0 change(s) in 60 s"));

    // Throttled from the start, the base from the runtime: nothing at 1x.
    CHECK((summaryText(shares({{2.0 * k90, 30.0}}, k90), 0) ==
           "base 11.11 ms (90 Hz); 1x 0.0%, 2x 100.0%; 0 change(s) in 30 s"));

    // Past kMaxMultiple, and more than three other rates.
    const RefreshShares many =
        shares({{5.0, 50.0}, {50.0, 10.0}, {8.0, 10.0}, {9.0, 20.0}, {7.0, 10.0}}, 5.0);
    CHECK(many.other == doctest::Approx(0.5));
    CHECK((summaryText(many, 4) == "base 5.00 ms (200 Hz); 1x 50.0%, other 50.0% (111 Hz 20.0%, 20 Hz 10.0%, "
                                   "125 Hz 10.0%, ...); 4 change(s) in 100 s"));

    CHECK(summaryText(shares({}, k90), 0).empty());
    CHECK(summaryText(shares({{k90, 10.0}}, 0.0), 0).empty());
}

TEST_CASE("status file values: the base refresh rate and the throttled share") {
    CHECK((refreshHzValue(k144) == "144.0"));
    CHECK((refreshHzValue(k90) == "90.0"));
    CHECK(refreshHzValue(0.0).empty());
    CHECK(
        (throttledShareValue(shares({{k144, 81.5}, {k72, 17.2}, {k48, 0.9}, {k90, 0.4}}, k144)) == "0.181"));
    CHECK((throttledShareValue(shares({{k90, 60.0}}, k90)) == "0.000"));
    CHECK(throttledShareValue(shares({}, k90)).empty());
}
