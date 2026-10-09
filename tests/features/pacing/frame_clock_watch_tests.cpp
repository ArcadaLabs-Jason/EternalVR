#include "features/pacing/frame_clock_watch.hpp"
#include "features/pacing/pace_policy.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <optional>

using namespace evr::pacing;

namespace {

constexpr std::int64_t kPeriodNs = 11'111'111; // 90 Hz
constexpr double kPeriod = 1.0 / 90.0;

// Feeds `frames` frames from `clock` on, each `step` seconds after the last, the display time moving by
// `advanceNs` and the game presenting `presentsPerFrame` times per frame; the first stall seen, if any.
std::optional<ClockStall> run(FrameClockWatch& watch,
                              double& clock,
                              std::int64_t& displayTime,
                              std::uint64_t& presents,
                              int frames,
                              double step,
                              std::int64_t advanceNs,
                              std::uint64_t presentsPerFrame,
                              bool shown = true) {
    for (int i = 0; i < frames; ++i) {
        displayTime += advanceNs;
        presents += presentsPerFrame;
        clock += step;
        if (const auto stall = watch.onFrame(displayTime, kPeriodNs, clock, presents, shown)) {
            return stall;
        }
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("frame clock watch: a runtime keeping time is never a stall") {
    FrameClockWatch watch;
    double clock = 0.0;
    std::int64_t displayTime = 1'000'000'000;
    std::uint64_t presents = 0;
    CHECK_FALSE(run(watch, clock, displayTime, presents, 90 * 60, kPeriod, kPeriodNs, 2).has_value());
}

TEST_CASE("frame clock watch: a display time that stops moving is a stall after kStuckFrames frames") {
    FrameClockWatch watch;
    double clock = 0.0;
    std::int64_t displayTime = 1'000'000'000;
    std::uint64_t presents = 0;
    CHECK_FALSE(run(watch, clock, displayTime, presents, 10, kPeriod, kPeriodNs, 2).has_value());
    // One nanosecond per frame, as SteamVR gave after the reset.
    CHECK_FALSE(run(watch, clock, displayTime, presents, FrameClockWatch::kStuckFrames - 1, kPeriod, 1, 2)
                    .has_value());
    const auto stall = run(watch, clock, displayTime, presents, 1, kPeriod, 1, 2);
    REQUIRE(stall.has_value());
    CHECK(stall->stuck);
    // It starts over: the next stall needs as many frames again.
    CHECK_FALSE(run(watch, clock, displayTime, presents, FrameClockWatch::kStuckFrames - 1, kPeriod, 1, 2)
                    .has_value());
}

TEST_CASE("frame clock watch: one normal frame breaks a stuck run") {
    FrameClockWatch watch;
    double clock = 0.0;
    std::int64_t displayTime = 0;
    std::uint64_t presents = 0;
    for (int i = 0; i < 10; ++i) {
        CHECK_FALSE(run(watch, clock, displayTime, presents, FrameClockWatch::kStuckFrames - 1, kPeriod, 1, 2)
                        .has_value());
        CHECK_FALSE(run(watch, clock, displayTime, presents, 1, kPeriod, kPeriodNs, 2).has_value());
    }
}

TEST_CASE("frame clock watch: few frames a second while the game presents is a stall after kSlowSeconds") {
    FrameClockWatch watch;
    double clock = 0.0;
    std::int64_t displayTime = 0;
    std::uint64_t presents = 0;
    // 4 frames a second, the display time moving on normally, the game at 200 presents a second.
    const auto stall = run(watch, clock, displayTime, presents, 40, 0.25, 250'000'000, 50);
    REQUIRE(stall.has_value());
    CHECK_FALSE(stall->stuck);
    CHECK(stall->framesPerSecond == doctest::Approx(4.0));
    CHECK(stall->presentsPerSecond == doctest::Approx(200.0));
    CHECK(clock == doctest::Approx(FrameClockWatch::kSlowSeconds + 0.25));
}

TEST_CASE("frame clock watch: slow frames are no stall while hidden or while the game is not presenting") {
    FrameClockWatch watch;
    double clock = 0.0;
    std::int64_t displayTime = 0;
    std::uint64_t presents = 0;
    CHECK_FALSE(run(watch, clock, displayTime, presents, 80, 0.25, 250'000'000, 50, false).has_value());
    CHECK_FALSE(run(watch, clock, displayTime, presents, 80, 0.25, 250'000'000, 0).has_value());
}

TEST_CASE("frame gaps: a runtime keeping time has none, its longest interval one period") {
    FrameGaps gaps;
    std::int64_t displayTime = 1'000'000'000;
    for (int i = 0; i < 900; ++i) {
        gaps.onFrame(displayTime, kPeriodNs);
        displayTime += kPeriodNs;
    }
    CHECK(gaps.gaps() == 0);
    CHECK(gaps.longestMs() == doctest::Approx(11.111111));
}

TEST_CASE("frame gaps: intervals over two periods count, and the longest is kept") {
    FrameGaps gaps;
    std::int64_t displayTime = 1'000'000'000;
    const auto frame = [&](std::int64_t advanceNs) {
        displayTime += advanceNs;
        gaps.onFrame(displayTime, kPeriodNs);
    };
    frame(0);
    frame(kPeriodNs);
    frame(2 * kPeriodNs);     // exactly two periods: no gap
    frame(2 * kPeriodNs + 1); // just over
    frame(kPeriodNs);
    frame(100'000'000); // a 100 ms stall
    frame(kPeriodNs);
    CHECK(gaps.gaps() == 2);
    CHECK(gaps.longestMs() == doctest::Approx(100.0));
}

TEST_CASE("frame gaps: a new period starts the counts over, the interval across it still counts") {
    FrameGaps gaps;
    gaps.onFrame(1'000'000'000, kPeriodNs);
    gaps.onFrame(1'050'000'000, kPeriodNs);
    REQUIRE(gaps.gaps() == 1);
    gaps.newPeriod();
    CHECK(gaps.gaps() == 0);
    CHECK(gaps.longestMs() == 0.0);
    gaps.onFrame(1'100'000'000, kPeriodNs);
    CHECK(gaps.gaps() == 1);
    CHECK(gaps.longestMs() == doctest::Approx(50.0));
}

TEST_CASE("frame gaps: no interval back to a past session, a stuck or backward clock, or without a period") {
    FrameGaps gaps;
    gaps.onFrame(1'000'000'000, kPeriodNs);
    gaps.restart();
    gaps.onFrame(9'000'000'000, kPeriodNs); // the next session, 8 s later
    CHECK(gaps.gaps() == 0);
    CHECK(gaps.longestMs() == 0.0);
    gaps.onFrame(9'000'000'000, kPeriodNs); // not moved on
    gaps.onFrame(8'000'000'000, kPeriodNs); // backwards
    CHECK(gaps.gaps() == 0);
    CHECK(gaps.longestMs() == 0.0);
    gaps.onFrame(8'100'000'000, 0); // no period: only the longest interval
    CHECK(gaps.gaps() == 0);
    CHECK(gaps.longestMs() == doctest::Approx(100.0));
}

TEST_CASE("copy wait: two display periods within the bounds") {
    CHECK(copyWaitMs(kPeriodNs) == 23);  // 90 Hz: 22.2 ms, rounded up
    CHECK(copyWaitMs(13'888'889) == 28); // 72 Hz
    CHECK(copyWaitMs(6'944'444) == 14);  // 144 Hz
    CHECK(copyWaitMs(0) == 28);          // none given: 72 Hz
    CHECK(copyWaitMs(-5) == 28);
    CHECK(copyWaitMs(1'000'000) == kMinCopyWaitMs);
    CHECK(copyWaitMs(1'000'000'000) == kMaxCopyWaitMs);
}

TEST_CASE("unfocused cap: one image per display period on the game's own clock") {
    constexpr double kPer = UnfocusedCap::kPeriodsPerImage;
    CHECK(UnfocusedCap::interval(kPeriod) == doctest::Approx(kPer * kPeriod));
    CHECK(UnfocusedCap::interval(1.0 / 144.0) == doctest::Approx(kPer * UnfocusedCap::kMinPeriodSeconds));
    CHECK(UnfocusedCap::interval(1.0) == doctest::Approx(kPer * UnfocusedCap::kMaxPeriodSeconds));
    CHECK(UnfocusedCap::interval(0.0) == doctest::Approx(kPer * UnfocusedCap::kDefaultPeriodSeconds));
    CHECK(UnfocusedCap::interval(std::numeric_limits<double>::quiet_NaN()) ==
          doctest::Approx(kPer * UnfocusedCap::kDefaultPeriodSeconds));

    const double interval = UnfocusedCap::interval(kPeriod);
    UnfocusedCap cap;
    CHECK(cap.afterHandOver(kPeriod, 10.0) == 0.0); // the first image goes on at once
    // A game at 200 images a second waits out the rest of each interval.
    CHECK(cap.afterHandOver(kPeriod, 10.005) == doctest::Approx(interval - 0.005));
    double now = 10.0 + interval;
    CHECK(cap.afterHandOver(kPeriod, now + 0.001) == doctest::Approx(interval - 0.001));
    now += interval;
    // A game slower than the interval never waits.
    CHECK(cap.afterHandOver(kPeriod, now + 0.05) == 0.0);
    CHECK(cap.afterHandOver(kPeriod, now + 0.1) == 0.0);
    CHECK(cap.afterHandOver(kPeriod, std::numeric_limits<double>::quiet_NaN()) == 0.0);
}
