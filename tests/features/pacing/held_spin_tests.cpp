#include "features/pacing/held_spin.hpp"

#include <doctest/doctest.h>

using namespace evr::pacing;

namespace {

constexpr double kPeriod = 1.0 / 90.0;

// Held after a hand-over 3 ms into the headset's frame 100: the next frame is due 8.1 ms from now.
HeldWaitInput heldAfterFall() {
    HeldWaitInput in;
    in.fellBehind = true;
    in.loop.frames = 100;
    in.loop.lastFrameSeconds = 10.0;
    in.loop.periodSeconds = kPeriod;
    in.step.wait = true;
    in.step.untilFrame = 101;
    in.step.timeoutSeconds = 2.0 * kPeriod;
    in.nowSeconds = 10.003;
    return in;
}

} // namespace

TEST_CASE("held spin: a menu or a loading screen never spins, in any mode, fallen behind or not") {
    for (const HeldSpin mode : {HeldSpin::Sleep, HeldSpin::OnceBehind, HeldSpin::Always}) {
        for (const bool fell : {false, true}) {
            HeldWaitInput in = heldAfterFall();
            in.mode = mode;
            in.fellBehind = fell;
            in.inPlay = false;
            const HeldWaitPlan plan = planHeldWait(in);
            CHECK_FALSE(plan.spin);
            CHECK(plan.sleepSeconds == doctest::Approx(in.step.timeoutSeconds));
        }
    }
}

TEST_CASE("held spin: the low hold never spins; before the latch, or with the rig's 0, it only sleeps") {
    HeldWaitInput low = heldAfterFall();
    low.low = true;
    CHECK_FALSE(planHeldWait(low).spin);
    HeldWaitInput before = heldAfterFall();
    before.fellBehind = false;
    CHECK_FALSE(planHeldWait(before).spin);
    HeldWaitInput off = heldAfterFall();
    off.mode = HeldSpin::Sleep;
    CHECK_FALSE(planHeldWait(off).spin);
}

TEST_CASE("held spin: in play after a fall it sleeps until 5 ms before the frame, then spins at most 6 ms") {
    const HeldWaitPlan plan = planHeldWait(heldAfterFall());
    REQUIRE(plan.spin);
    CHECK(plan.sleepSeconds == doctest::Approx(10.0 + kPeriod - 0.005 - 10.003));
    CHECK(plan.spinLimitSeconds == doctest::Approx(kHeldSpinSeconds + kHeldSpinOverSeconds));
}

TEST_CASE("held spin: a late frame (already due) spins at most the spin plus 1 ms, not the whole timeout") {
    HeldWaitInput in = heldAfterFall();
    in.nowSeconds = 10.0 + kPeriod + 0.002; // the frame was due 2 ms ago
    const HeldWaitPlan plan = planHeldWait(in);
    REQUIRE(plan.spin);
    CHECK(plan.sleepSeconds == doctest::Approx(0.0));
    CHECK(plan.spinLimitSeconds == doctest::Approx(0.006));
    CHECK(plan.spinLimitSeconds < in.step.timeoutSeconds);
    // Not known when it is due (no period yet): no sleep, the same cap.
    HeldWaitInput unknown = heldAfterFall();
    unknown.loop.periodSeconds = 0.0;
    const HeldWaitPlan u = planHeldWait(unknown);
    CHECK(u.sleepSeconds == doctest::Approx(0.0));
    CHECK(u.spinLimitSeconds == doctest::Approx(0.006));
}

TEST_CASE("held spin: sleep and spin stay within the timeout; the rig's 1 spins the whole wait") {
    HeldWaitInput far = heldAfterFall();
    far.step.untilFrame = 103; // three periods ahead, past the two-period timeout
    const HeldWaitPlan plan = planHeldWait(far);
    CHECK(plan.sleepSeconds + plan.spinLimitSeconds <= far.step.timeoutSeconds + 1e-12);
    HeldWaitInput always = heldAfterFall();
    always.mode = HeldSpin::Always;
    always.fellBehind = false;
    const HeldWaitPlan a = planHeldWait(always);
    CHECK(a.spin);
    CHECK(a.sleepSeconds == doctest::Approx(0.0));
    CHECK(a.spinLimitSeconds == doctest::Approx(always.step.timeoutSeconds));
}
