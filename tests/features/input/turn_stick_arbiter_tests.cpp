#include "features/input/turn_stick_arbiter.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <ostream>
#include <vector>

using evr::input::Axis2;
using evr::input::SweepIntent;
using evr::input::TurnStickArbiter;
using evr::input::TurnStickOutput;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

// Stick at `degrees` clockwise from straight up, at full deflection.
Axis2 stickAt(float degrees, float deflection = 1.0f) {
    const float radians = degrees * std::numbers::pi_v<float> / 180.0f;
    return {deflection * std::sin(radians), deflection * std::cos(radians)};
}

// `frames` stick positions at full deflection, starting at `fromDegrees` and moving `stepDegrees` per
// frame until `toDegrees`, where the stick then stays.
std::vector<Axis2> arc(float fromDegrees, float toDegrees, float stepDegrees, int frames) {
    std::vector<Axis2> positions;
    for (int i = 0; i < frames; ++i) {
        const float unclamped = fromDegrees + stepDegrees * static_cast<float>(i);
        positions.push_back(
            stickAt(stepDegrees > 0.0f ? std::min(unclamped, toDegrees) : std::max(unclamped, toDegrees)));
    }
    return positions;
}

struct SweepResult {
    int turnFrames = 0;
    int taps = 0;
    int holdFrames = 0;
    int upFrames = 0;
};

// Feeds a sequence of stick positions, then centres the stick.
template <typename Positions>
SweepResult runSweep(TurnStickArbiter& arbiter, const Positions& positions) {
    SweepResult result;
    auto count = [&result](const TurnStickOutput& output) {
        result.turnFrames += output.turnAllowed ? 1 : 0;
        result.taps += output.downTap ? 1 : 0;
        result.holdFrames += output.downHold ? 1 : 0;
        result.upFrames += output.up ? 1 : 0;
    };
    for (const Axis2 position : positions) {
        count(arbiter.update(position, kFrame));
    }
    count(arbiter.update({}, kFrame));
    return result;
}

} // namespace

TEST_CASE("a quick deliberate pull down is a tap on recentre") {
    TurnStickArbiter arbiter;
    const std::array<Axis2, 4> sweep{{{0.0f, -0.4f}, {0.0f, -0.9f}, {0.0f, -1.0f}, {0.0f, -0.6f}}};
    const SweepResult result = runSweep(arbiter, sweep);
    CHECK(result.taps == 1);
    CHECK(result.holdFrames == 0);
    CHECK(result.turnFrames == 0);
}

TEST_CASE("holding down opens the wheel and the stick then points freely") {
    TurnStickArbiter arbiter;
    arbiter.update({0.0f, -1.0f}, kFrame);
    TurnStickOutput output;
    for (int i = 0; i < 30; ++i) {
        output = arbiter.update({0.0f, -1.0f}, kFrame);
    }
    CHECK(output.downHold);

    // Pointing sideways at the wheel keeps it open and never turns.
    output = arbiter.update({1.0f, 0.0f}, kFrame);
    CHECK(output.downHold);
    CHECK_FALSE(output.turnAllowed);
    CHECK(output.wheelPointer == Axis2{1.0f, 0.0f});

    // Releasing closes the wheel without a quick switch.
    output = arbiter.update({}, kFrame);
    CHECK_FALSE(output.downHold);
    CHECK_FALSE(output.downTap);
}

TEST_CASE("a button holding the wheel makes the stick its pointer, whatever the sweep") {
    TurnStickArbiter arbiter;
    // A turn in progress when the button goes down stops turning; the stick points instead.
    CHECK(arbiter.update({1.0f, 0.0f}, kFrame).turnAllowed);
    for (const Axis2 stick : {Axis2{1.0f, 0.0f}, Axis2{0.0f, 1.0f}, Axis2{0.0f, -1.0f}, Axis2{}}) {
        for (int i = 0; i < 40; ++i) {
            const TurnStickOutput output = arbiter.update(stick, kFrame, true);
            CHECK_FALSE(output.turnAllowed);
            CHECK_FALSE(output.up);
            CHECK_FALSE(output.downTap);
            CHECK_FALSE(output.downHold);
            CHECK(output.wheelPointer == stick);
        }
    }
}

TEST_CASE("a stick still pointing when the wheel's button is let go does nothing until it recentres") {
    for (const Axis2 stick : {Axis2{0.0f, 1.0f}, Axis2{1.0f, 0.0f}, Axis2{0.0f, -1.0f}}) {
        TurnStickArbiter arbiter;
        arbiter.update(stick, kFrame, true);
        std::vector<Axis2> after(40, stick);
        const SweepResult result = runSweep(arbiter, after);
        CHECK(result.turnFrames == 0);
        CHECK(result.upFrames == 0);
        CHECK(result.taps == 0);
        CHECK(result.holdFrames == 0);
        // The next sweep is claimed as usual.
        CHECK(arbiter.update({0.0f, 1.0f}, kFrame).up);
    }
}

TEST_CASE("a down sweep cut short by the wheel's button is no quick switch") {
    TurnStickArbiter arbiter;
    arbiter.update({0.0f, -1.0f}, kFrame);
    arbiter.update({0.0f, -1.0f}, kFrame, true);
    arbiter.update({}, kFrame, true);
    CHECK_FALSE(arbiter.update({}, kFrame).downTap);
}

TEST_CASE("a turning sweep that drifts down never selects a weapon") {
    TurnStickArbiter arbiter;
    // Right turn, then the thumb rolls down through the down cone and back up, held long enough to
    // open the wheel if it counted.
    const SweepResult result = runSweep(arbiter, arc(90.0f, 267.0f, 3.0f, 60));
    CHECK(result.turnFrames > 0);
    CHECK(result.taps == 0);
    CHECK(result.holdFrames == 0);
    CHECK(result.upFrames == 0);
}

TEST_CASE("a turn that ends held straight down never opens the wheel") {
    TurnStickArbiter arbiter;
    // Right, rolling down to straight down, then held there well past the hold time.
    const SweepResult result = runSweep(arbiter, arc(90.0f, 180.0f, 3.0f, 100));
    CHECK(result.taps == 0);
    CHECK(result.holdFrames == 0);
}

TEST_CASE("sweeps across the bottom in both directions never select a weapon") {
    for (const float direction : {1.0f, -1.0f}) {
        TurnStickArbiter arbiter;
        // From one side through straight down to the other side.
        const SweepResult result = runSweep(
            arbiter, arc(180.0f + 90.0f * direction, 180.0f - 90.0f * direction, -4.5f * direction, 40));
        CHECK(result.taps == 0);
        CHECK(result.holdFrames == 0);
    }
}

TEST_CASE("a down sweep that leaves the cone before the hold time is cancelled") {
    TurnStickArbiter arbiter;
    const std::array sweep{Axis2{0.0f, -1.0f}, stickAt(160.0f), stickAt(120.0f), stickAt(100.0f)};
    const SweepResult result = runSweep(arbiter, sweep);
    CHECK(result.taps == 0);
    CHECK(result.turnFrames == 0);
}

TEST_CASE("a down sweep with a small sideways wobble still counts") {
    TurnStickArbiter arbiter;
    const std::array sweep{stickAt(185.0f), stickAt(170.0f), stickAt(195.0f)};
    CHECK(runSweep(arbiter, sweep).taps == 1);
}

TEST_CASE("a shallow diagonal pull does not claim the down gesture") {
    TurnStickArbiter arbiter;
    // 40 degrees off vertical is outside the 30 degree claim cone: this is a turn.
    const std::array sweep{stickAt(140.0f), stickAt(140.0f)};
    const SweepResult result = runSweep(arbiter, sweep);
    CHECK(result.taps == 0);
    CHECK(result.turnFrames == 2);
}

TEST_CASE("a partial pull below the engage threshold does nothing") {
    TurnStickArbiter arbiter;
    const std::array<Axis2, 2> sweep{{{0.0f, -0.5f}, {0.0f, -0.6f}}};
    const SweepResult result = runSweep(arbiter, sweep);
    CHECK(result.taps == 0);
    CHECK(result.turnFrames == 0);
}

TEST_CASE("up is held for the whole up sweep and never turns") {
    TurnStickArbiter arbiter;
    const std::array sweep{Axis2{0.0f, 0.9f}, stickAt(60.0f), stickAt(90.0f)};
    const SweepResult result = runSweep(arbiter, sweep);
    CHECK(result.upFrames == 3);
    CHECK(result.turnFrames == 0);
}

TEST_CASE("each sweep gets a fresh claim after recentring") {
    TurnStickArbiter arbiter;
    arbiter.update({1.0f, 0.0f}, kFrame);
    CHECK(arbiter.intent() == SweepIntent::Turn);
    arbiter.update({}, kFrame);
    CHECK(arbiter.intent() == SweepIntent::None);
    arbiter.update({0.0f, -1.0f}, kFrame);
    CHECK(arbiter.intent() == SweepIntent::Down);
}

TEST_CASE("a non-finite stick mid down sweep does not end it as a quick switch") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    TurnStickArbiter arbiter;
    arbiter.update({0.0f, -1.0f}, kFrame);
    const TurnStickOutput glitch = arbiter.update({nan, nan}, kFrame);
    CHECK_FALSE(glitch.downTap);
    CHECK(arbiter.intent() == SweepIntent::Down);
    // The sweep carries on and ends normally.
    arbiter.update({0.0f, -1.0f}, kFrame);
    CHECK(arbiter.update({}, kFrame).downTap);
}

TEST_CASE("non-finite stick frames do not advance the hold timer") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    TurnStickArbiter arbiter;
    arbiter.update({0.0f, -1.0f}, kFrame);
    for (int i = 0; i < 100; ++i) {
        CHECK_FALSE(arbiter.update({nan, 0.0f}, kFrame).downHold);
    }
    CHECK(arbiter.update({}, kFrame).downTap);
}

TEST_CASE("a non-finite stick mid turn keeps the turn claimed") {
    TurnStickArbiter arbiter;
    CHECK(arbiter.update({1.0f, 0.0f}, kFrame).turnAllowed);
    CHECK(arbiter.update({std::numeric_limits<float>::infinity(), 0.0f}, kFrame).turnAllowed);
    CHECK(arbiter.intent() == SweepIntent::Turn);
}

TEST_CASE("invalid arbiter settings fall back to the defaults") {
    const evr::input::TurnStickSettings defaults;
    evr::input::TurnStickSettings nanRadius;
    nanRadius.centreRadius = std::numeric_limits<float>::quiet_NaN();
    evr::input::TurnStickSettings claimInsideCentre;
    claimInsideCentre.turnClaim = 0.1f;
    evr::input::TurnStickSettings stayNarrowerThanClaim;
    stayNarrowerThanClaim.stayConeDegrees = 10.0f;
    evr::input::TurnStickSettings negativeHold;
    negativeHold.holdSeconds = -1.0f;
    for (const auto& settings : {nanRadius, claimInsideCentre, stayNarrowerThanClaim, negativeHold}) {
        const TurnStickArbiter arbiter(settings);
        CHECK(arbiter.settings().centreRadius == defaults.centreRadius);
        CHECK(arbiter.settings().turnClaim == defaults.turnClaim);
        CHECK(arbiter.settings().stayConeDegrees == defaults.stayConeDegrees);
        CHECK(arbiter.settings().holdSeconds == defaults.holdSeconds);
    }

    // With a NaN centre radius the stick would never count as centred and every sweep would last
    // forever.
    TurnStickArbiter arbiter(nanRadius);
    arbiter.update({1.0f, 0.0f}, kFrame);
    arbiter.update({}, kFrame);
    CHECK(arbiter.intent() == SweepIntent::None);
}

TEST_CASE("a cancelled sweep does nothing more until the stick is back in the centre") {
    TurnStickArbiter arbiter;
    arbiter.update({0.0f, -1.0f}, kFrame);
    REQUIRE(arbiter.intent() == SweepIntent::Down);
    arbiter.cancelSweep();
    CHECK(arbiter.intent() == SweepIntent::Cancelled);
    for (int i = 0; i < 60; ++i) {
        const TurnStickOutput out = arbiter.update({0.0f, -1.0f}, kFrame);
        CHECK_FALSE(out.downHold);
        CHECK_FALSE(out.turnAllowed);
    }
    CHECK_FALSE(arbiter.update({}, kFrame).downTap); // no quick switch
    CHECK(arbiter.intent() == SweepIntent::None);

    // With the stick centred there is no sweep to cancel: the next one claims as usual.
    arbiter.cancelSweep();
    arbiter.update({0.0f, -1.0f}, kFrame);
    CHECK(arbiter.update({}, kFrame).downTap);
}
