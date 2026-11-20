#include "features/comfort/launch_pulse.hpp"
#include "features/comfort/vignette.hpp"

#include <doctest/doctest.h>

#include <limits>

using namespace evr::comfort;

namespace {

constexpr double kFrame = 1.0 / 90.0;

} // namespace

TEST_CASE("launch pulse: the hold is sized to the launch") {
    // A pad's flight time, a little more; else a throw straight up at the launch speed.
    CHECK(launchHoldSeconds({1.5f, 0.0f}) == doctest::Approx(1.75f));
    CHECK(launchHoldSeconds({0.0f, 21.0f}) == doctest::Approx(2.0f * 21.0f / kLaunchGravity));
    // The flight time wins over the speed.
    CHECK(launchHoldSeconds({1.0f, 21.0f}) == doctest::Approx(1.25f));
    // Within the limits.
    CHECK(launchHoldSeconds({0.0f, 2.0f}) == kMinLaunchHoldSeconds);
    CHECK(launchHoldSeconds({0.0f, 75.0f}) == kMaxLaunchHoldSeconds);
    CHECK(launchHoldSeconds({10.0f, 0.0f}) == kMaxLaunchHoldSeconds);
    // Unknown: the default.
    CHECK(launchHoldSeconds({}) == kDefaultLaunchHoldSeconds);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(launchHoldSeconds({nan, nan}) == kDefaultLaunchHoldSeconds);
    CHECK(launchHoldSeconds({-1.0f, -5.0f}) == kDefaultLaunchHoldSeconds);
}

TEST_CASE("launch pulse: in fast, held while the launch carries the player, out gently") {
    const LaunchPulseTiming timing;
    LaunchPulse pulse(timing);
    CHECK(pulse.amount(10.0) == 0.0f);
    CHECK_FALSE(pulse.active(10.0));
    pulse.launch(10.0, 3.0f);
    CHECK(pulse.amount(10.0) == 0.0f); // starts from what was shown
    CHECK(pulse.active(10.0));
    // In: rising, full by the end of the rise (well before the stick's 0.2 s).
    const float early = pulse.amount(10.0 + timing.riseSeconds / 2.0);
    CHECK(early > 0.2f);
    CHECK(early < 0.8f);
    CHECK(pulse.amount(10.0 + timing.riseSeconds) == 1.0f);
    // Off the ground: held at full however long the flight, up to its limit.
    pulse.ground(10.05, false);
    pulse.ground(11.5, false);
    CHECK(pulse.amount(11.5) == 1.0f);
    // Down at 12.2 s: the hold ends there and the fall follows, monotonic and smooth.
    pulse.ground(12.2, true);
    CHECK(pulse.landed());
    CHECK(pulse.landedAt() == doctest::Approx(12.2));
    CHECK(pulse.holdEnd() == doctest::Approx(12.2));
    CHECK(pulse.amount(12.19) == 1.0f);
    float last = 1.0f;
    for (double t = 12.2; t <= 12.2 + timing.fallSeconds + 0.1; t += kFrame) {
        const float a = pulse.amount(t);
        CHECK(a <= last);
        CHECK(last - a < 0.05f); // no step bigger than a twentieth a frame (smooth)
        last = a;
    }
    CHECK(pulse.amount(12.2 + timing.fallSeconds + 0.001) == 0.0f);
    CHECK_FALSE(pulse.active(12.2 + timing.fallSeconds + 0.001));
    // A later landing changes nothing.
    pulse.ground(13.0, false);
    pulse.ground(13.5, true);
    CHECK(pulse.landedAt() == doctest::Approx(12.2));
}

TEST_CASE("launch pulse: the hold has a minimum and a limit") {
    const LaunchPulseTiming timing;
    {
        // Down again at once (a low pad): still full for the minimum.
        LaunchPulse pulse(timing);
        pulse.launch(10.0, 2.0f);
        pulse.ground(10.02, false);
        pulse.ground(10.1, true);
        CHECK(pulse.holdEnd() == doctest::Approx(10.0 + timing.minHoldSeconds));
        CHECK(pulse.amount(10.0 + timing.minHoldSeconds - 0.01) == 1.0f);
    }
    {
        // Never seen landing (a read that stopped): the limit ends it.
        LaunchPulse pulse(timing);
        pulse.launch(10.0, 1.5f);
        pulse.ground(10.02, false);
        CHECK(pulse.amount(11.49) == 1.0f);
        CHECK(pulse.amount(11.5 + timing.fallSeconds + 0.001) == 0.0f);
        CHECK_FALSE(pulse.active(11.5 + timing.fallSeconds + 0.001));
    }
    {
        // Landing after the limit: the limit wins.
        LaunchPulse pulse(timing);
        pulse.launch(10.0, 1.0f);
        pulse.ground(10.02, false);
        pulse.ground(13.0, true);
        CHECK(pulse.holdEnd() == doctest::Approx(11.0));
    }
    {
        // A cap below the minimum hold is raised to it.
        LaunchPulse pulse(timing);
        pulse.launch(10.0, 0.0f);
        CHECK(pulse.holdEnd() >= 10.0 + timing.minHoldSeconds);
    }
}

TEST_CASE("launch pulse: a launch that never lifts the player ends soon") {
    const LaunchPulseTiming timing;
    LaunchPulse pulse(timing);
    pulse.launch(10.0, 3.0f);
    // On the ground in the launch's own frame and after: no landing yet (the velocity was just set) ...
    pulse.ground(10.0, true);
    pulse.ground(10.2, true);
    CHECK_FALSE(pulse.landed());
    // ... until leaveGroundSeconds have gone without leaving it.
    pulse.ground(10.0 + timing.leaveGroundSeconds, true);
    CHECK(pulse.landed());
    CHECK(pulse.holdEnd() == doctest::Approx(10.0 + timing.leaveGroundSeconds));
}

TEST_CASE("launch pulse: a second launch during the first carries on with no dip") {
    const LaunchPulseTiming timing;
    LaunchPulse pulse(timing);
    pulse.launch(10.0, 2.0f);
    pulse.ground(10.05, false);
    pulse.ground(11.0, true);
    // Halfway through the fall a chained pad throws the player again.
    const double second = 11.0 + timing.fallSeconds / 2.0;
    const float before = pulse.amount(second);
    CHECK(before > 0.0f);
    CHECK(before < 1.0f);
    pulse.launch(second, 2.0f);
    CHECK(pulse.amount(second) == doctest::Approx(before));
    CHECK(pulse.amount(second + kFrame) >= before);
    CHECK(pulse.amount(second + timing.riseSeconds) == 1.0f);
    CHECK_FALSE(pulse.landed()); // the new launch has its own landing
    pulse.ground(second + 0.05, false);
    pulse.ground(second + 1.0, true);
    CHECK(pulse.holdEnd() == doctest::Approx(second + 1.0));
}

TEST_CASE("launch pulse: nothing before a launch, after a reset, or for times that are not finite") {
    LaunchPulse pulse;
    pulse.ground(10.0, false);
    pulse.ground(10.5, true);
    CHECK_FALSE(pulse.landed());
    CHECK(pulse.amount(10.5) == 0.0f);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    pulse.launch(nan, 2.0f);
    CHECK_FALSE(pulse.active(10.0));
    pulse.launch(10.0, std::numeric_limits<float>::quiet_NaN()); // the default limit
    CHECK(pulse.holdEnd() == doctest::Approx(10.0 + kDefaultLaunchHoldSeconds));
    CHECK(pulse.amount(nan) == 0.0f);
    CHECK(pulse.amount(9.0) == 0.0f); // before the launch
    pulse.reset();
    CHECK(pulse.amount(10.5) == 0.0f);
    CHECK_FALSE(pulse.active(10.5));
}

TEST_CASE("launch pulse: bad timing values take their defaults") {
    LaunchPulseTiming bad;
    bad.riseSeconds = -1.0f;
    bad.minHoldSeconds = std::numeric_limits<float>::quiet_NaN();
    bad.fallSeconds = 100.0f;
    bad.leaveGroundSeconds = -2.0f;
    LaunchPulse pulse(bad);
    const LaunchPulseTiming d;
    pulse.launch(10.0, 0.0f);
    CHECK(pulse.amount(10.0 + d.riseSeconds) == 1.0f);
    CHECK(pulse.holdEnd() == doctest::Approx(10.0 + d.minHoldSeconds));
    CHECK(pulse.amount(10.0 + d.minHoldSeconds + d.fallSeconds + 0.001) == 0.0f);
}

TEST_CASE("vignette policy: a launch's pulse shows as it is, over the rate-limited amount") {
    VignettePolicy policy;
    VignetteMotion motion;
    motion.launch = 1.0f;
    // Full in the first frame: the pulse has its own rise, the policy's 0.2 s does not slow it.
    CHECK(policy.update(motion, kFrame) == 1.0f);
    CHECK(policy.value() == 1.0f);
    // The policy's own amount stays as the other motion makes it: the pulse going leaves it clear.
    motion.launch = 0.0f;
    CHECK(policy.update(motion, kFrame) == 0.0f);
    // With the stick held, the larger of the two shows.
    motion.moveMagnitude = 1.0f;
    for (int i = 0; i < 60; ++i) {
        policy.update(motion, kFrame);
    }
    motion.launch = 0.3f;
    CHECK(policy.update(motion, kFrame) == 1.0f);
    // A launch is not a target: nothing else changes for it.
    VignetteMotion launchOnly;
    launchOnly.launch = 1.0f;
    CHECK(vignetteTarget(launchOnly, VignetteTiming{}) == 0.0f);
    // Out of range and not finite: clamped, or none.
    VignettePolicy other;
    VignetteMotion odd;
    odd.launch = 5.0f;
    CHECK(other.update(odd, kFrame) == 1.0f);
    odd.launch = std::numeric_limits<float>::quiet_NaN();
    CHECK(other.update(odd, kFrame) == 0.0f);
    other.reset();
    CHECK(other.value() == 0.0f);
}
