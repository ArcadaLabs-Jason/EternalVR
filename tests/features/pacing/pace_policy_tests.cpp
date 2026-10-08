#include "features/pacing/pace_policy.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>

using namespace evr::pacing;

namespace {

constexpr double kPeriod = 1.0 / 90.0;

HeadsetLoop loopAt(std::uint64_t frames, double lastFrame, double period = kPeriod) {
    HeadsetLoop loop;
    loop.frames = frames;
    loop.lastFrameSeconds = lastFrame;
    loop.periodSeconds = period;
    return loop;
}

} // namespace

TEST_CASE("pace mode: names parse back, any case and spacing") {
    for (const PaceMode mode : {PaceMode::Off, PaceMode::Headset}) {
        const auto parsed = parsePaceMode(paceModeName(mode));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == mode);
    }
    CHECK(parsePaceMode(" Headset ") == PaceMode::Headset);
    CHECK(parsePaceMode("OFF") == PaceMode::Off);
    CHECK_FALSE(parsePaceMode("").has_value());
    CHECK_FALSE(parsePaceMode("on").has_value());
    CHECK_FALSE(parsePaceMode("headsets").has_value());
}

TEST_CASE("pacer off: never waits, still counts the images handed over") {
    FramePacer pacer(PaceMode::Off);
    for (int i = 0; i < 5; ++i) {
        CHECK_FALSE(pacer.afterHandOver(loopAt(10, 1.0), 1.001).wait);
    }
    CHECK(pacer.counters().handOvers == 5);
    CHECK(pacer.counters().waits == 0);
    CHECK(pacer.counters().idle == 0);
}

TEST_CASE("pacer: a game faster than the headset waits once per headset frame for the next one") {
    FramePacer pacer(PaceMode::Headset);
    double t = 2.0;
    std::uint64_t frames = 40;
    // The first image after the loop is known: a frame began since the (zero) release, so no wait.
    CHECK_FALSE(pacer.afterHandOver(loopAt(frames, t), t + 0.004).wait);
    for (int i = 0; i < 90; ++i) {
        // The game finishes its next image 4 ms into the headset's frame, before the next one begins.
        const PaceStep step = pacer.afterHandOver(loopAt(frames, t), t + 0.004);
        REQUIRE(step.wait);
        CHECK(step.untilFrame == frames + 1);
        CHECK(step.timeoutSeconds == doctest::Approx(2.0 * kPeriod));
        // The headset's next frame begins; the game goes on with it.
        ++frames;
        t += kPeriod;
        pacer.waited(frames, kPeriod - 0.004, true);
    }
    const FramePacer::Counters& c = pacer.counters();
    CHECK(c.handOvers == 91);
    CHECK(c.waits == 90);
    CHECK(c.timeouts == 0);
    CHECK(c.frameBegun == 1);
    CHECK(c.waitSeconds == doctest::Approx(90.0 * (kPeriod - 0.004)));
    CHECK(c.longestWaitSeconds == doctest::Approx(kPeriod - 0.004));
    CHECK(pacer.takeLongestWait() == doctest::Approx(kPeriod - 0.004));
    CHECK(pacer.takeLongestWait() == 0.0);
}

TEST_CASE("pacer: a game slower than the headset never waits") {
    FramePacer pacer(PaceMode::Headset);
    double t = 5.0;
    std::uint64_t frames = 100;
    for (int i = 0; i < 30; ++i) {
        // Each image takes longer than a period: a headset frame always began meanwhile.
        frames += 1 + (i % 3 == 0 ? 1 : 0);
        t += kPeriod;
        CHECK_FALSE(pacer.afterHandOver(loopAt(frames, t), t + 0.002).wait);
    }
    CHECK(pacer.counters().frameBegun == 30);
    CHECK(pacer.counters().waits == 0);
}

TEST_CASE("pacer: a missed headset frame times out, and a stopped loop lets the game run free") {
    FramePacer pacer(PaceMode::Headset);
    const double last = 10.0;
    CHECK_FALSE(pacer.afterHandOver(loopAt(7, last), last + 0.003).wait);
    PaceStep step = pacer.afterHandOver(loopAt(7, last), last + 0.003);
    REQUIRE(step.wait);
    // No frame comes: the wait times out after two periods and counts a miss.
    pacer.waited(7, step.timeoutSeconds, false);
    CHECK(pacer.counters().timeouts == 1);
    // A stall (a streaming runtime's encoder behind): the next images still wait, two periods each.
    double now = last + 0.003 + step.timeoutSeconds + 0.002;
    step = pacer.afterHandOver(loopAt(7, last), now);
    REQUIRE(step.wait);
    CHECK(step.timeoutSeconds == doctest::Approx(FramePacer::kTimeoutPeriods * kPeriod));
    pacer.waited(7, step.timeoutSeconds, false);
    // Near the point where the loop counts as stopped (ten periods after its last frame): the wait ends
    // there.
    now = last + 9.5 * kPeriod;
    step = pacer.afterHandOver(loopAt(7, last), now);
    REQUIRE(step.wait);
    CHECK(step.timeoutSeconds == doctest::Approx(last + FramePacer::kIdlePeriods * kPeriod - now));
    pacer.waited(7, step.timeoutSeconds, false);
    CHECK(pacer.counters().timeouts == 3);
    // From then on: no waits while no headset frame comes.
    for (int i = 0; i < 10; ++i) {
        CHECK_FALSE(pacer.afterHandOver(loopAt(7, last), last + 0.12 + 0.01 * i).wait);
    }
    CHECK(pacer.counters().idle == 10);
    CHECK(pacer.counters().timeouts == 3);
    // The loop runs again: the first image goes on at once, the next waits again.
    CHECK_FALSE(pacer.afterHandOver(loopAt(8, last + 1.0), last + 1.002).wait);
    CHECK(pacer.afterHandOver(loopAt(8, last + 1.0), last + 1.006).wait);
}

TEST_CASE("pacer: no wait before the loop is known, or with a period that makes no sense") {
    FramePacer pacer(PaceMode::Headset);
    CHECK_FALSE(pacer.afterHandOver(HeadsetLoop{}, 1.0).wait);
    CHECK_FALSE(pacer.afterHandOver(loopAt(3, 1.0, 0.0), 1.001).wait);
    CHECK_FALSE(pacer.afterHandOver(loopAt(3, 1.0, -kPeriod), 1.001).wait);
    CHECK_FALSE(pacer.afterHandOver(loopAt(3, 1.0, std::numeric_limits<double>::quiet_NaN()), 1.001).wait);
    CHECK_FALSE(pacer.afterHandOver(loopAt(3, std::numeric_limits<double>::infinity()), 1.001).wait);
    CHECK(pacer.counters().idle == 5);
    // A period that reads far too long still bounds the wait.
    CHECK_FALSE(pacer.afterHandOver(loopAt(4, 1.0, 1.0), 1.001).wait);
    const PaceStep step = pacer.afterHandOver(loopAt(4, 1.0, 1.0), 1.002);
    REQUIRE(step.wait);
    CHECK(step.timeoutSeconds == doctest::Approx(FramePacer::kMaxTimeoutSeconds));
}

TEST_CASE("pacer: a slow refresh rate does not stretch the held window past its cap") {
    // 30 Hz: ten periods would be 333 ms; the loop counts as stopped after kMaxIdleSeconds.
    FramePacer pacer(PaceMode::Headset);
    const double period = 1.0 / 30.0;
    const double last = 2.0;
    CHECK_FALSE(pacer.afterHandOver(loopAt(5, last, period), last + 0.001).wait);
    const PaceStep step = pacer.afterHandOver(loopAt(5, last, period), last + 0.1);
    REQUIRE(step.wait);
    CHECK(step.timeoutSeconds == doctest::Approx(FramePacer::kMaxIdleSeconds - 0.1));
    pacer.waited(5, step.timeoutSeconds, false);
    CHECK_FALSE(
        pacer.afterHandOver(loopAt(5, last, period), last + FramePacer::kMaxIdleSeconds + 0.001).wait);
    CHECK(pacer.counters().idle == 1);
}

TEST_CASE("cadence: images per headset frame, the ones never shown, first showings' lateness") {
    Cadence cadence;
    cadence.frame(500); // the baseline only
    CHECK(cadence.counters().frames == 0);
    // A game at about 1.5 times the headset's rate: one, two, one, two ...
    std::uint64_t handed = 500;
    for (int i = 0; i < 10; ++i) {
        handed += (i % 2 == 0) ? 1 : 2;
        cadence.frame(handed);
    }
    cadence.frame(handed); // a repeat
    const Cadence::Counters& c = cadence.counters();
    CHECK(c.frames == 11);
    CHECK(c.one == 5);
    CHECK(c.several == 5);
    CHECK(c.none == 1);
    CHECK(c.notShown == 5);
    CHECK(c.handOvers == 15);

    cadence.shown(0, 0.5);    // no view: not counted
    cadence.shown(42, 0.011); // first showing
    cadence.shown(42, 0.022); // the same game frame again: not counted
    cadence.shown(43, 0.013);
    cadence.shown(44, std::numeric_limits<double>::quiet_NaN());
    CHECK(cadence.counters().shownViews == 2);
    CHECK(cadence.counters().lateSeconds == doctest::Approx(0.024));
}
