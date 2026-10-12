#include "stereo_seq/present_watch.hpp"

#include <doctest/doctest.h>

using evr::stereo_seq::OneShotAfter;
using evr::stereo_seq::parseTestSeconds;
using evr::stereo_seq::PresentWatch;

TEST_CASE("present watch: presents that keep coming are never a stop") {
    PresentWatch watch(5.0);
    CHECK(watch.check(0, 0.0) == PresentWatch::Event::None); // the first check starts the clock
    CHECK(watch.check(900, 10.0) == PresentWatch::Event::None);
    CHECK(watch.check(1800, 20.0) == PresentWatch::Event::None);
    CHECK(watch.check(1801, 30.0) == PresentWatch::Event::None); // slow, not stopped
}

TEST_CASE("present watch: a count that stops moving is reported once, then its return") {
    PresentWatch watch(5.0);
    CHECK(watch.check(100, 0.0) == PresentWatch::Event::None);
    CHECK(watch.check(500, 10.0) == PresentWatch::Event::None);
    CHECK(watch.check(500, 14.0) == PresentWatch::Event::None); // 4 s: not yet
    REQUIRE(watch.check(500, 20.0) == PresentWatch::Event::Stopped);
    CHECK(watch.quiet() == doctest::Approx(10.0));
    CHECK(watch.check(500, 30.0) == PresentWatch::Event::None); // reported once
    CHECK(watch.check(500, 120.0) == PresentWatch::Event::None);
    REQUIRE(watch.check(501, 130.0) == PresentWatch::Event::Resumed);
    CHECK(watch.quiet() == doctest::Approx(120.0));
    CHECK(watch.check(900, 140.0) == PresentWatch::Event::None);
    // A second stop is reported again.
    CHECK(watch.check(900, 150.0) == PresentWatch::Event::Stopped);
}

TEST_CASE("present watch: a count that never moved from the first check is a stop too") {
    PresentWatch watch(5.0);
    CHECK(watch.check(42, 0.0) == PresentWatch::Event::None);
    CHECK(watch.check(42, 10.0) == PresentWatch::Event::Stopped);
}

TEST_CASE("test seconds setting") {
    CHECK(parseTestSeconds(L"60").value_or(0.0) == doctest::Approx(60.0));
    CHECK(parseTestSeconds(L" 2.5 ").value_or(0.0) == doctest::Approx(2.5));
    CHECK_FALSE(parseTestSeconds(L"").has_value());
    CHECK_FALSE(parseTestSeconds(L"0").has_value());
    CHECK_FALSE(parseTestSeconds(L"-3").has_value());
    CHECK_FALSE(parseTestSeconds(L"10s").has_value());
    CHECK_FALSE(parseTestSeconds(L"abc").has_value());
    CHECK_FALSE(parseTestSeconds(L"nan").has_value());
    CHECK_FALSE(parseTestSeconds(L"100000").has_value()); // over a day
}

TEST_CASE("one shot: due once, at the first call the given time after the first") {
    OneShotAfter shot(30.0);
    CHECK_FALSE(shot.due(100.0)); // the first present starts the clock
    CHECK_FALSE(shot.due(129.9));
    CHECK(shot.due(130.0));
    CHECK_FALSE(shot.due(131.0));
    CHECK_FALSE(shot.due(1000.0));
}

TEST_CASE("stop cause: the newest failed present, and whether it was the last") {
    using evr::stereo_seq::kResultOutOfDate;
    using evr::stereo_seq::kResultSurfaceLost;
    using evr::stereo_seq::PresentFailure;
    using evr::stereo_seq::stopCause;
    CHECK(stopCause(0, 0, 500).failure == PresentFailure::None);
    CHECK(stopCause(kResultOutOfDate, 500, 500).failure == PresentFailure::OutOfDate);
    CHECK(stopCause(kResultOutOfDate, 500, 500).lastPresent);
    CHECK(stopCause(kResultSurfaceLost, 500, 500).failure == PresentFailure::OutOfDate);
    CHECK_FALSE(stopCause(kResultOutOfDate, 499, 500).lastPresent);  // a present came after it
    CHECK(stopCause(-4, 500, 500).failure == PresentFailure::Other); // VK_ERROR_DEVICE_LOST
}

TEST_CASE("stop at start: the last present within two minutes of the first check") {
    using evr::stereo_seq::kStopAtStartSeconds;
    using evr::stereo_seq::stopAtStart;
    CHECK(stopAtStart(1000.0 + 14.0, 1000.0));
    CHECK(stopAtStart(1000.0 + kStopAtStartSeconds, 1000.0));
    CHECK_FALSE(stopAtStart(1000.0 + kStopAtStartSeconds + 1.0, 1000.0));
}

namespace {

using evr::stereo_seq::PresentFailure;
using evr::stereo_seq::StopCause;
using evr::stereo_seq::StopRecovery;
using evr::stereo_seq::StopScene;

constexpr StopCause kOutOfDateLast{PresentFailure::OutOfDate, true};
constexpr StopScene kShown{};
constexpr StopScene kMinimised{.minimised = true};
constexpr StopScene kOtherDesktop{.otherDesktop = true};

} // namespace

TEST_CASE("stop recovery: only a stop right after an out of date present, once") {
    StopRecovery recovery;
    CHECK_FALSE(recovery.due(false, kOutOfDateLast, kShown)); // presenting: the scene of this check is kept
    // No stop, or a stop without an out of date last present: nothing.
    CHECK_FALSE(recovery.due(false, kOutOfDateLast, kShown));
    CHECK_FALSE(recovery.due(true, StopCause{PresentFailure::None, false}, kShown));
    CHECK_FALSE(recovery.due(true, StopCause{PresentFailure::Other, true}, kShown));
    // Out of date at an earlier present, presents went on after it: the stop has another cause.
    CHECK_FALSE(recovery.due(true, StopCause{PresentFailure::OutOfDate, false}, kShown));
    // The start-up stall: the last present before the stop was out of date.
    CHECK(recovery.due(true, kOutOfDateLast, kShown));
    // Once per process.
    CHECK_FALSE(recovery.due(true, kOutOfDateLast, kShown));
}

TEST_CASE("stop recovery: never while the window is minimised or the input is on another desktop") {
    StopRecovery recovery;
    CHECK_FALSE(recovery.due(false, {}, kShown));
    // Minimised: the game's surface has no size and its last present was out of date; it waits, not stuck.
    CHECK_FALSE(recovery.due(true, kOutOfDateLast, kMinimised));
    CHECK_FALSE(recovery.due(true, kOutOfDateLast, kMinimised));
    // Restored just before this check: the game has not presented yet; not reported at this check.
    CHECK_FALSE(recovery.due(true, kOutOfDateLast, kShown));
    // Presents came back: nothing, and the one report is still there for a real stop.
    CHECK_FALSE(recovery.due(false, kOutOfDateLast, kShown));
    // The lock screen or a UAC prompt.
    CHECK_FALSE(recovery.due(true, kOutOfDateLast, kOtherDesktop));
    CHECK_FALSE(recovery.due(true, kOutOfDateLast, StopScene{.minimised = true, .otherDesktop = true}));
    CHECK_FALSE(recovery.due(true, kOutOfDateLast, kShown));
    // Still stopped with the window shown at two checks in a row: stuck, reported once.
    CHECK(recovery.due(true, kOutOfDateLast, kShown));
    CHECK_FALSE(recovery.due(true, kOutOfDateLast, kShown));
}

TEST_CASE("present watch: stopped and the last move, for the stop report") {
    PresentWatch watch(5.0);
    CHECK(watch.check(10, 0.0) == PresentWatch::Event::None);
    CHECK(watch.check(20, 10.0) == PresentWatch::Event::None);
    CHECK_FALSE(watch.stopped());
    CHECK(watch.check(20, 20.0) == PresentWatch::Event::Stopped);
    CHECK(watch.stopped());
    CHECK(watch.lastMoved() == doctest::Approx(10.0));
    CHECK(watch.check(20, 30.0) == PresentWatch::Event::None);
    CHECK(watch.stopped()); // until the count moves
    CHECK(watch.check(21, 40.0) == PresentWatch::Event::Resumed);
    CHECK_FALSE(watch.stopped());
    CHECK(watch.lastMoved() == doctest::Approx(40.0));
}
