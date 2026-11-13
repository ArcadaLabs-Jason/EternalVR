#include "gpu_timing/present_hang.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using evr::gpu_timing::HangAction;
using evr::gpu_timing::HangCheck;
using evr::gpu_timing::HangWatch;
using evr::gpu_timing::kHangFirstNoteMs;
using evr::gpu_timing::kHangSecondNoteMs;
using evr::gpu_timing::kHangsNoted;
using evr::gpu_timing::parseTestHang;

namespace {

constexpr std::uint64_t kSecond = 1000000;
constexpr std::uint64_t kPresent = 100 * kSecond; // the last present before the gap

// In play: the last tick 5 ms before the last present.
HangCheck at(double secondsAfterPresent, std::uint64_t lastPresent = kPresent) {
    HangCheck c;
    c.lastPresentMicros = lastPresent;
    c.lastTickMicros = lastPresent - 5000;
    c.ticksKnown = true;
    c.nowMicros = lastPresent + static_cast<std::uint64_t>(secondsAfterPresent * kSecond);
    return c;
}

} // namespace

TEST_CASE("present hang: nothing before the first present or below the first note") {
    HangWatch watch;
    HangCheck none;
    none.nowMicros = 60 * kSecond;
    CHECK(watch.onCheck(none).action == HangAction::None);
    CHECK(watch.onCheck(at(0.011)).action == HangAction::None);
    CHECK(watch.onCheck(at(kHangFirstNoteMs / 1000.0 - 0.01)).action == HangAction::None);
}

TEST_CASE("present hang: a gap in play gets a note at the first and the second mark, once each") {
    HangWatch watch;
    const auto first = watch.onCheck(at(kHangFirstNoteMs / 1000.0));
    CHECK(first.action == HangAction::FirstNote);
    CHECK(first.gapMs == doctest::Approx(kHangFirstNoteMs));
    CHECK(first.tickAgeMs == doctest::Approx(kHangFirstNoteMs + 5.0));
    CHECK(first.hang == 1);
    CHECK(watch.onCheck(at(6.0)).action == HangAction::None);
    CHECK(watch.onCheck(at(kHangSecondNoteMs / 1000.0)).action == HangAction::SecondNote);
    CHECK(watch.onCheck(at(60.0)).action == HangAction::None);
    CHECK(watch.onCheck(at(600.0)).action == HangAction::None);
}

TEST_CASE("present hang: a present after a noted gap is said once, with the whole gap") {
    HangWatch watch;
    CHECK(watch.onCheck(at(5.5)).action == HangAction::FirstNote);
    const std::uint64_t next = kPresent + 8 * kSecond;
    const auto resumed = watch.onCheck(at(0.001, next));
    CHECK(resumed.action == HangAction::Resumed);
    CHECK(resumed.gapMs == doctest::Approx(8000.0));
    CHECK(resumed.hang == 1);
    CHECK(watch.onCheck(at(0.012, next)).action == HangAction::None);
    // A later gap is a new hang.
    const auto again = watch.onCheck(at(5.0, next + kSecond));
    CHECK(again.action == HangAction::FirstNote);
    CHECK(again.hang == 2);
}

TEST_CASE("present hang: an unnoted stall that ends is not called a resume") {
    HangWatch watch;
    CHECK(watch.onCheck(at(3.0)).action == HangAction::None);
    CHECK(watch.onCheck(at(0.01, kPresent + 3 * kSecond)).action == HangAction::None);
}

TEST_CASE("present hang: loading screens and menus are not hangs; without ticks every gap is") {
    HangWatch watch;
    HangCheck loading = at(30.0);
    loading.lastTickMicros = kPresent - 2 * kSecond; // the game stopped ticking before its last present
    CHECK(watch.onCheck(loading).action == HangAction::None);
    HangCheck noTick = at(30.0);
    noTick.lastTickMicros = 0; // the title screen before the first map
    CHECK(watch.onCheck(noTick).action == HangAction::None);
    HangCheck tickAfter = at(9.0);
    tickAfter.lastTickMicros = kPresent + 1000; // the game ticked once more, then stopped presenting
    CHECK(watch.onCheck(tickAfter).action == HangAction::FirstNote);

    HangWatch unknown;
    HangCheck cinema = at(9.0);
    cinema.ticksKnown = false;
    cinema.lastTickMicros = 0;
    const auto r = unknown.onCheck(cinema);
    CHECK(r.action == HangAction::FirstNote);
    CHECK(r.tickAgeMs < 0.0);
}

TEST_CASE("present hang: at most kHangsNoted gaps a process are noted") {
    HangWatch watch;
    std::uint64_t present = kPresent;
    for (std::uint32_t i = 1; i <= kHangsNoted; ++i) {
        CHECK(watch.onCheck(at(5.0, present)).action == HangAction::FirstNote);
        present += 10 * kSecond;
        CHECK(watch.onCheck(at(0.001, present)).action == HangAction::Resumed);
    }
    CHECK(watch.onCheck(at(30.0, present)).action == HangAction::None);
}

TEST_CASE("present hang: the test hang's value") {
    const auto seconds = parseTestHang(L"30");
    REQUIRE(seconds);
    CHECK(seconds->ms == 30000);
    CHECK(seconds->atSecondsInMap < 0.0);
    const auto ms = parseTestHang(L"700ms");
    REQUIRE(ms);
    CHECK(ms->ms == 700);
    CHECK(ms->atSecondsInMap < 0.0);
    const auto at = parseTestHang(L"700ms@20.5");
    REQUIRE(at);
    CHECK(at->ms == 700);
    CHECK(at->atSecondsInMap == doctest::Approx(20.5));
    const auto atWhole = parseTestHang(L"2@15");
    REQUIRE(atWhole);
    CHECK(atWhole->ms == 2000);
    CHECK(atWhole->atSecondsInMap == doctest::Approx(15.0));
    for (const wchar_t* bad : {L"", L"0", L"0ms", L"ms", L"x", L"700 ms", L"-5", L"700ms@", L"700ms@x",
                               L"700ms@1.", L"601", L"600001ms", L"1.5"}) {
        CHECK_FALSE(parseTestHang(bad));
    }
    CHECK(parseTestHang(L"600")->ms == 600000);
}
