#include "stereo_seq/adaptive_eyes.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

using evr::stereo_seq::AdaptiveConfig;
using evr::stereo_seq::AdaptiveEyes;
using evr::stereo_seq::AlternateMode;
using evr::stereo_seq::alternateMode;
using evr::stereo_seq::cpuLoadMsAt;
using evr::stereo_seq::CpuLoadSpec;
using evr::stereo_seq::parseCpuLoad;
using evr::stereo_seq::TickSample;

namespace {

// Ticks at `rate` per second for `seconds`, each of the way the policy asks for (both eyes: with eye R
// taking `rightShare` of the tick). Returns the seconds into the run of the first switch, if any.
struct Run {
    AdaptiveEyes policy;
    double now = 0.0;
    int switches = 0;

    explicit Run(AdaptiveConfig c = {}) : policy(c) { policy.setDisplayPeriod(1.0 / 90.0); }

    std::optional<double> ticks(double rate, double seconds, double rightShare = 0.45, bool stereo = true) {
        std::optional<double> first;
        const double period = 1.0 / rate;
        const auto n = static_cast<int>(seconds * rate + 0.5);
        for (int i = 0; i < n; ++i) {
            TickSample t;
            t.seconds = period;
            t.stereo = stereo;
            t.pairedTick = policy.pairs();
            t.rightSeconds = t.pairedTick ? period * rightShare : 0.0;
            now += period;
            if (policy.onTick(t)) {
                ++switches;
                if (!first) {
                    first = now;
                }
            }
        }
        return first;
    }
};

} // namespace

TEST_CASE("adaptive eyes: ETERNALVR_ALTERNATE_EYES values") {
    CHECK(alternateMode("auto") == AlternateMode::Auto);
    CHECK(alternateMode(" AUTO ") == AlternateMode::Auto);
    CHECK(alternateMode("1") == AlternateMode::On);
    CHECK(alternateMode("on") == AlternateMode::On);
    CHECK(alternateMode("True") == AlternateMode::On);
    CHECK(alternateMode("0") == AlternateMode::Off);
    CHECK(alternateMode("") == AlternateMode::Off);
    CHECK(alternateMode("automatic") == AlternateMode::Off);
    CHECK(alternateMode("yes") == AlternateMode::Off); // as switchValue: not a switch word
}

TEST_CASE("adaptive eyes: both eyes per tick while the processor keeps up with the headset") {
    Run r;
    CHECK(r.policy.pairs());
    CHECK_FALSE(r.ticks(95.0, 30.0));
    CHECK(r.policy.pairs());
    // Just under the display rate but above 97% of it: still both eyes.
    CHECK_FALSE(r.ticks(88.0, 30.0));
    CHECK(r.policy.pairs());
}

TEST_CASE("adaptive eyes: alternates after about a second below the display rate") {
    Run r;
    const std::optional<double> at = r.ticks(60.0, 5.0);
    REQUIRE(at);
    CHECK(*at >= 0.99);
    CHECK(*at < 1.3);
    CHECK_FALSE(r.policy.pairs());
    CHECK(r.policy.stats().switches[1] == 1);
}

TEST_CASE("adaptive eyes: a short dip does not switch, and mono time starts the count again") {
    Run r;
    CHECK_FALSE(r.ticks(60.0, 0.7));
    CHECK_FALSE(r.ticks(100.0, 1.0)); // back up: the count starts again
    CHECK_FALSE(r.ticks(60.0, 0.7));
    CHECK_FALSE(r.ticks(60.0, 0.5, 0.45, false)); // a menu
    CHECK_FALSE(r.ticks(60.0, 0.7));
    CHECK(r.policy.pairs());
    CHECK(r.policy.stats().monoSeconds > 0.4);
}

TEST_CASE("adaptive eyes: eye R's share of a Route S tick is measured") {
    Run r;
    r.ticks(95.0, 10.0, 0.4); // eye R 0.4 of the tick: 0.4 / 0.6 of the rest
    CHECK(r.policy.share() == doctest::Approx(0.4 / 0.6).epsilon(0.02));
    Run slow;
    slow.ticks(95.0, 10.0, 0.05); // clamped at shareMin
    CHECK(slow.policy.share() == doctest::Approx(AdaptiveConfig{}.shareMin));
}

TEST_CASE("adaptive eyes: stays alternating while both eyes per tick would still be too slow") {
    Run r;
    r.ticks(95.0, 5.0, 0.45); // share about 0.82
    REQUIRE(r.ticks(60.0, 2.0));
    // Alternating at 160 ticks/s: both eyes per tick estimated at about 88, under 90 * 1.15.
    const int before = r.switches;
    r.ticks(160.0, 20.0);
    CHECK(r.switches == before);
    CHECK_FALSE(r.policy.pairs());
    CHECK(r.policy.lastEstimate() == doctest::Approx(160.0 / (1.0 + r.policy.share())).epsilon(0.01));
}

TEST_CASE("adaptive eyes: pairs again after about three seconds comfortably above the display rate") {
    Run r;
    REQUIRE(r.ticks(60.0, 2.0));
    const double start = r.now;
    // 250 ticks/s alternating: both eyes per tick estimated at about 139, above 103.5.
    const std::optional<double> back = r.ticks(250.0, 10.0);
    REQUIRE(back);
    // Measured in quarter-second windows: the first one may have started before.
    CHECK(*back - start >= 2.7);
    CHECK(*back - start < 3.6);
    CHECK(r.policy.pairs());
    CHECK(r.policy.stats().switches[0] == 1);
}

TEST_CASE("adaptive eyes: flipping back soon doubles the wait, up to a limit, and a calm spell resets it") {
    Run r;
    REQUIRE(r.ticks(60.0, 2.0));
    CHECK(r.policy.offAfter() == doctest::Approx(3.0));
    REQUIRE(r.ticks(250.0, 4.0));
    // Too slow again within 10 s: alternating, and the next wait is twice as long.
    REQUIRE(r.ticks(60.0, 2.0));
    CHECK(r.policy.offAfter() == doctest::Approx(6.0));
    double start = r.now;
    std::optional<double> back = r.ticks(250.0, 10.0);
    REQUIRE(back);
    CHECK(*back - start >= 5.7);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(r.ticks(60.0, 2.0));
        REQUIRE(r.ticks(250.0, r.policy.offAfter() + 1.0));
    }
    CHECK(r.policy.offAfter() == doctest::Approx(AdaptiveConfig{}.offAfterMax));
    // Both eyes per tick for a while (over flapWithin), then too slow: the wait starts again at 3 s.
    CHECK_FALSE(r.ticks(100.0, 20.0));
    REQUIRE(r.ticks(60.0, 2.0));
    CHECK(r.policy.offAfter() == doctest::Approx(3.0));
    start = r.now;
    back = r.ticks(250.0, 10.0);
    REQUIRE(back);
    CHECK(*back - start < 3.6);
}

TEST_CASE("adaptive eyes: a tick of the other way is not measured") {
    AdaptiveEyes p;
    p.setDisplayPeriod(1.0 / 90.0);
    // Both eyes per tick asked, but eye R did not render (an alternating tick): ignored, no switch however
    // slow.
    for (int i = 0; i < 300; ++i) {
        TickSample t;
        t.seconds = 1.0 / 30.0;
        t.stereo = true;
        t.pairedTick = false;
        CHECK_FALSE(p.onTick(t));
    }
    CHECK(p.pairs());
    CHECK(p.stats().ticks[1] == 300);
    CHECK(p.stats().ticks[0] == 0);
}

TEST_CASE("adaptive eyes: the display rate follows the headset within bounds") {
    AdaptiveEyes p;
    CHECK(p.displayHz() == doctest::Approx(90.0));
    p.setDisplayPeriod(1.0 / 120.0);
    CHECK(p.displayHz() == doctest::Approx(120.0));
    p.setDisplayPeriod(0.0);
    p.setDisplayPeriod(1.0);
    CHECK(p.displayHz() == doctest::Approx(120.0));
    // A 72 Hz headset at 70 ticks/s: below 97% of 72, so it alternates.
    Run r;
    r.policy.setDisplayPeriod(1.0 / 72.0);
    CHECK(r.ticks(69.0, 3.0));
    Run keeps;
    keeps.policy.setDisplayPeriod(1.0 / 72.0);
    CHECK_FALSE(keeps.ticks(71.0, 10.0));
}

TEST_CASE("test CPU load: its values and the on-off square wave") {
    REQUIRE(parseCpuLoad("6"));
    CHECK(parseCpuLoad("6")->ms == doctest::Approx(6.0));
    CHECK(parseCpuLoad(" 2.5 ")->ms == doctest::Approx(2.5));
    CHECK_FALSE(parseCpuLoad(""));
    CHECK_FALSE(parseCpuLoad("0"));
    CHECK_FALSE(parseCpuLoad("-1"));
    CHECK_FALSE(parseCpuLoad("101"));
    CHECK_FALSE(parseCpuLoad("6ms"));
    CHECK_FALSE(parseCpuLoad("6,30"));
    CHECK_FALSE(parseCpuLoad("6,30,20,1"));
    CHECK_FALSE(parseCpuLoad("6,0,20"));
    const std::optional<CpuLoadSpec> wave = parseCpuLoad("8,30,20");
    REQUIRE(wave);
    CHECK(cpuLoadMsAt(*wave, 0.0) == doctest::Approx(8.0));
    CHECK(cpuLoadMsAt(*wave, 29.9) == doctest::Approx(8.0));
    CHECK(cpuLoadMsAt(*wave, 30.1) == doctest::Approx(0.0));
    CHECK(cpuLoadMsAt(*wave, 49.9) == doctest::Approx(0.0));
    CHECK(cpuLoadMsAt(*wave, 50.1) == doctest::Approx(8.0));
    CHECK(cpuLoadMsAt(*parseCpuLoad("4"), 1000.0) == doctest::Approx(4.0));
    CHECK(cpuLoadMsAt(*parseCpuLoad("4,10,0"), 15.0) == doctest::Approx(4.0)); // no off period: always on
}
