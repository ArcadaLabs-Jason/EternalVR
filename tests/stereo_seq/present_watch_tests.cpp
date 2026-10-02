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
