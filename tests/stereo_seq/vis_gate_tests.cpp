#include "stereo_seq/vis_gate.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using evr::stereo_seq::visGateContinues;

namespace {

// The engine's bookkeeping for one model over a run of renders: counted renders store counter + 1 and grow
// the count when the gate continues, else restart it at 1. Returns the count after the last render.
int countAfter(const bool (&seen)[8], std::int32_t renders) {
    std::int32_t lastVisible = 0;
    int count = 0;
    for (std::int32_t counter = 1; counter <= 8; ++counter) {
        if (!seen[counter - 1]) {
            continue;
        }
        count = visGateContinues(lastVisible, counter, renders) ? count + 1 : 1;
        lastVisible = counter + 1;
    }
    return count;
}

} // namespace

TEST_CASE("vis gate: the engine's test continues only after the previous render") {
    CHECK(visGateContinues(10, 10, 1));      // counted in render 9
    CHECK_FALSE(visGateContinues(9, 10, 1)); // counted in render 8
    CHECK(visGateContinues(11, 10, 1));      // counted this render already
}

TEST_CASE("vis gate: two renders accepts the render before the previous one, no older") {
    CHECK(visGateContinues(10, 10, 2));
    CHECK(visGateContinues(9, 10, 2));
    CHECK_FALSE(visGateContinues(8, 10, 2));
}

TEST_CASE("vis gate: a model one eye sees counts up under Route S with two renders, never with one") {
    // Route S: odd counters are eye L's renders, even ones eye R's; the model is in eye R's frustum only.
    const bool eyeROnly[8] = {false, true, false, true, false, true, false, true};
    CHECK(countAfter(eyeROnly, 1) == 1); // restarts every render: stays under firstVisibleFrameCount 2
    CHECK(countAfter(eyeROnly, 2) == 4); // one step per tick: passes the gate (> 2) on the third tick
    const bool both[8] = {true, true, true, true, true, true, true, true};
    CHECK(countAfter(both, 1) == 8);
    CHECK(countAfter(both, 2) == 8);
}

TEST_CASE("update in view: the engine's test is the previous render only; two renders adds the one before") {
    using evr::stereo_seq::updateInViewContinues;
    CHECK(updateInViewContinues(9, 10, 1));
    CHECK_FALSE(updateInViewContinues(8, 10, 1));
    CHECK_FALSE(updateInViewContinues(10, 10, 1)); // already updated this render
    CHECK(updateInViewContinues(8, 10, 2));
    CHECK(updateInViewContinues(9, 10, 2));
    CHECK_FALSE(updateInViewContinues(7, 10, 2));
    CHECK_FALSE(updateInViewContinues(10, 10, 2));
    CHECK_FALSE(updateInViewContinues(11, 10, 2));         // a stamp ahead of the frame never passes
    CHECK(updateInViewContinues(INT32_MAX, INT32_MIN, 1)); // the frame number wrapped
    CHECK(updateInViewContinues(INT32_MAX - 1, INT32_MIN, 2));
}

TEST_CASE("update in view: a system one eye sees simulates once per tick under Route S with two renders") {
    using evr::stereo_seq::updateInViewContinues;
    // Frames 1..8 alternate eye L (odd) and eye R (even); the system is in eye R's view only, and an update
    // stamps the frame it ran in.
    const std::int32_t rendersTried[] = {1, 2};
    for (const std::int32_t renders : rendersTried) {
        std::int32_t stamp = -100; // last updated long ago
        int simulated = 0;
        for (std::int32_t frame = 2; frame <= 8; frame += 2) {
            simulated += updateInViewContinues(stamp, frame, renders) ? 1 : 0;
            stamp = frame;
        }
        CHECK(simulated ==
              (renders == 1 ? 0 : 3)); // never with the engine's test; every tick after the first
    }
}

TEST_CASE("vis gate: no overflow at the ends of the counter's range") {
    CHECK(visGateContinues(INT32_MIN, INT32_MIN, 2));
    CHECK_FALSE(visGateContinues(INT32_MIN, INT32_MIN + 2, 2));
}
