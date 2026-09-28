#include "features/roomscale/follow_test_steps.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <ostream>

using evr::Vec3;
using evr::test::approxEqual;
using namespace evr::roomscale;

TEST_CASE("test steps: out and back along the axis, each leg held, the list repeating") {
    TestSteps steps{{0.02f, 0.10f}, 2.0f, false};
    CHECK(testStepAt(steps, -1.0).leg == -1);
    CHECK(approxEqual(testStepAt(steps, -1.0).offset, {}));
    const TestStepPhase first = testStepAt(steps, 0.5);
    CHECK(first.leg == 0);
    CHECK(first.legs == 4);
    CHECK(first.asked == doctest::Approx(0.02f));
    CHECK(approxEqual(first.offset, {0.0f, 0.0f, -0.02f}));
    const TestStepPhase back = testStepAt(steps, 2.5);
    CHECK(back.leg == 1);
    CHECK(back.asked == doctest::Approx(-0.02f));
    CHECK(approxEqual(back.offset, {}));
    CHECK(back.started == doctest::Approx(2.0));
    CHECK(approxEqual(testStepAt(steps, 4.1).offset, {0.0f, 0.0f, -0.10f}));
    // The second pass starts over.
    const TestStepPhase again = testStepAt(steps, 8.2);
    CHECK(again.leg == 4);
    CHECK(approxEqual(again.offset, {0.0f, 0.0f, -0.02f}));
    steps.sideways = true;
    CHECK(approxEqual(testStepAt(steps, 0.1).offset, {0.02f, 0.0f, 0.0f}));
    CHECK(testStepAt(TestSteps{}, 1.0).leg == -1);
}

TEST_CASE("the step probe: along, across, overshoot, time to 90 % and what the anchor took") {
    StepProbe probe;
    CHECK_FALSE(probe.active());
    probe.begin(3, 0.10f, {0.0f, 0.0f, -1.0f}, 10.0);
    probe.add({0.01f, 0.0f, -0.05f}, {0.0f, 0.0f, -0.05f}, 10.1);
    probe.add({0.0f, 0.0f, -0.045f}, {0.0f, 0.0f, -0.045f}, 10.2);
    probe.add({0.0f, 0.0f, -0.015f}, {0.0f, 0.0f, -0.005f}, 10.3); // overshoot, mostly not absorbed
    probe.add({0.0f, 0.0f, 0.01f}, {}, 10.4);
    const StepReport r = probe.report();
    CHECK(r.leg == 3);
    CHECK(r.asked == doctest::Approx(0.10f));
    CHECK(r.along == doctest::Approx(0.10f));
    CHECK(r.peak == doctest::Approx(0.11f));
    CHECK(r.across == doctest::Approx(0.01f));
    CHECK(r.absorbed == doctest::Approx(0.10f));
    CHECK(r.reachSeconds == doctest::Approx(0.2));
    CHECK(r.seconds == doctest::Approx(0.4));

    // The way back counts in the asked sense.
    probe.begin(4, -0.10f, {0.0f, 0.0f, -1.0f}, 20.0);
    probe.add({0.0f, 0.0f, 0.04f}, {0.0f, 0.0f, 0.04f}, 20.5);
    const StepReport back = probe.report();
    CHECK(back.along == doctest::Approx(0.04f));
    CHECK(back.absorbed == doctest::Approx(0.04f));
    CHECK(back.reachSeconds < 0.0);
}

TEST_CASE("the command test: each value for one hold, then a stop leg; the probe's steady speed") {
    TestSteps moves{{40.0f, -80.0f}, 1.5f, false, true};
    const TestStepPhase first = testStepAt(moves, 0.2);
    CHECK(first.command == 40);
    CHECK(approxEqual(first.offset, {}));
    CHECK(testStepAt(moves, 1.6).command == 0);
    CHECK(testStepAt(moves, 3.1).command == -80);

    StepProbe probe;
    probe.begin(0, 1.0f, {0.0f, 0.0f, -1.0f}, 0.0, 1.0);
    double t = 0.0;
    for (int i = 0; i < 100; ++i) {
        t += 0.01;
        // Still for 50 ms, then 1 m/s forward.
        probe.add(i < 5 ? Vec3{} : Vec3{0.0f, 0.0f, -0.01f}, {}, t);
    }
    const StepReport r = probe.report();
    CHECK(r.along == doctest::Approx(0.95f));
    CHECK(r.speed == doctest::Approx(1.0f).epsilon(0.02));
    CHECK(r.firstMotion == doctest::Approx(0.06));
}

TEST_CASE("the step probe tracks the gap: where it started, where it ended, when it closed") {
    StepProbe probe;
    probe.begin(0, 0.2f, {0.0f, 0.0f, -1.0f}, 0.0);
    probe.add({}, {}, 0.1, 0.18f);
    probe.add({}, {}, 0.2, 0.05f);
    probe.add({}, {}, 0.3, -1.0f); // blocked: not known
    probe.add({}, {}, 0.4, 0.015f);
    probe.add({}, {}, 0.5, 0.012f);
    const StepReport r = probe.report();
    CHECK(r.gapStart == doctest::Approx(0.18f));
    CHECK(r.gapLeft == doctest::Approx(0.012f));
    CHECK(r.closeSeconds == doctest::Approx(0.4));
}
