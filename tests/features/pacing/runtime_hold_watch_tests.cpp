#include "features/pacing/runtime_hold_watch.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

using evr::pacing::RuntimeHold;
using evr::pacing::RuntimeHoldWatch;

namespace {

constexpr double kPeriodMs = 1000.0 / 90.0;

// 10 s windows fed to a watch, the XR frame counter running at `framesPerSecond`.
struct Feed {
    RuntimeHoldWatch watch;
    double seconds = 0.0;
    std::uint64_t frames = 0;

    std::optional<RuntimeHold>
    window(double endFrameMs, double framesPerSecond = 90.0, std::uint64_t calls = 900) {
        seconds += 10.0;
        frames += static_cast<std::uint64_t>(framesPerSecond * 10.0);
        return watch.onWindow(endFrameMs, calls, kPeriodMs, frames, seconds);
    }
};

} // namespace

TEST_CASE("a player's long Virtual Desktop session: the hold is named once it lasts 30 s, and its end") {
    Feed feed;
    for (int i = 0; i < 240; ++i) { // 40 minutes at ~0.4 ms
        CHECK_FALSE(feed.window(0.4));
    }
    CHECK_FALSE(feed.window(2.0)); // under a quarter of the period: not held
    CHECK_FALSE(feed.window(3.0, 88.0));
    CHECK_FALSE(feed.window(7.0, 79.0));
    const std::optional<RuntimeHold> began = feed.window(9.0, 79.0);
    REQUIRE(began);
    CHECK(began->began);
    CHECK(began->endFrameMs == doctest::Approx((3.0 + 7.0 + 9.0) / 3.0));
    CHECK(began->framesPerSecond == doctest::Approx((88.0 + 79.0 + 79.0) / 3.0));
    CHECK(began->lowestMs == doctest::Approx(0.4));
    CHECK(feed.watch.holding());
    CHECK_FALSE(feed.window(10.0, 79.0)); // one line per hold

    // The trip out of the game and back.
    CHECK_FALSE(feed.window(0.8));
    CHECK_FALSE(feed.window(0.6));
    const std::optional<RuntimeHold> ended = feed.window(0.5);
    REQUIRE(ended);
    CHECK_FALSE(ended->began);
    CHECK(ended->heldSeconds == doctest::Approx(40.0)); // the four held windows
    CHECK_FALSE(feed.watch.holding());
    CHECK(feed.watch.holds() == 1);
}

TEST_CASE("one slow window, or two, is not a hold; a window back under the line starts the count again") {
    Feed feed;
    for (int i = 0; i < 5; ++i) {
        CHECK_FALSE(feed.window(5.0));
        CHECK_FALSE(feed.window(5.0));
        CHECK_FALSE(feed.window(0.5));
    }
    CHECK_FALSE(feed.watch.holding());
    CHECK(feed.watch.holds() == 0);
}

TEST_CASE("windows without xrEndFrame calls (no session) are skipped, not counted either way") {
    Feed feed;
    CHECK_FALSE(feed.window(5.0));
    CHECK_FALSE(feed.window(0.0, 0.0, 0));
    CHECK_FALSE(feed.window(5.0));
    CHECK(feed.window(5.0));
}

TEST_CASE("the share is of the display period: 3 ms holds a 120 Hz headset's frames, not a 72 Hz one's") {
    RuntimeHoldWatch at120;
    RuntimeHoldWatch at72;
    std::optional<RuntimeHold> fast;
    std::optional<RuntimeHold> slow;
    for (int i = 1; i <= RuntimeHoldWatch::kWindows; ++i) {
        fast = at120.onWindow(3.0, 1200, 1000.0 / 120.0, 1200ULL * static_cast<std::uint64_t>(i), 10.0 * i);
        slow = at72.onWindow(3.0, 720, 1000.0 / 72.0, 720ULL * static_cast<std::uint64_t>(i), 10.0 * i);
    }
    CHECK(fast);
    CHECK_FALSE(slow);
}

TEST_CASE("at most kMaxLines lines; later holds are only counted") {
    Feed feed;
    std::uint32_t lines = 0;
    for (int hold = 0; hold < 8; ++hold) {
        for (int i = 0; i < RuntimeHoldWatch::kWindows; ++i) {
            lines += feed.window(6.0) ? 1 : 0;
        }
        for (int i = 0; i < RuntimeHoldWatch::kWindows; ++i) {
            lines += feed.window(0.5) ? 1 : 0;
        }
    }
    CHECK(lines == RuntimeHoldWatch::kMaxLines);
    CHECK(feed.watch.holds() == 8);
}
