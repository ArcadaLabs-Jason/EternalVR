#include "features/pacing/late_wait_gate.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <string>

using namespace evr::pacing;

TEST_CASE("the late-image wait runs only when asked for, matched to the headset, with both eyes each frame") {
    LateWaitAsk ask;
    CHECK_FALSE(lateWaitOn(ask));
    CHECK(lateWaitRefusal(ask) == LateWaitRefusal::None); // not asked for: nothing to say
    ask.asked = true;
    CHECK(lateWaitRefusal(ask) == LateWaitRefusal::NotHeadset);
    CHECK_FALSE(lateWaitOn(ask));
    ask.headset = true;
    CHECK(lateWaitRefusal(ask) == LateWaitRefusal::None);
    CHECK(lateWaitOn(ask));
    ask.parallelEyes = true;
    CHECK(lateWaitRefusal(ask) == LateWaitRefusal::ParallelEyes);
    CHECK_FALSE(lateWaitOn(ask));
    ask.parallelEyes = false;
    ask.alternateEyes = true;
    CHECK(lateWaitRefusal(ask) == LateWaitRefusal::AlternateEyes);
    CHECK_FALSE(lateWaitOn(ask));
    ask.asked = false; // a refusal only matters when asked for
    CHECK(lateWaitRefusal(ask) == LateWaitRefusal::None);
    for (const auto r :
         {LateWaitRefusal::NotHeadset, LateWaitRefusal::ParallelEyes, LateWaitRefusal::AlternateEyes}) {
        CHECK_FALSE(std::string(lateWaitRefusalText(r)).empty());
    }
    CHECK(std::string(lateWaitRefusalText(LateWaitRefusal::None)).empty());
}

TEST_CASE("the GPU share estimate rises with a fence wait at once") {
    GpuShareEstimate e;
    CHECK(e.seconds() == 0.0);
    e.note(0.0);
    CHECK(e.seconds() == 0.0);
    e.note(0.005);
    CHECK(e.seconds() == doctest::Approx(0.005));
    e.note(0.006);
    CHECK(e.seconds() == doctest::Approx(0.006));
}

TEST_CASE("the estimate comes down only after a clean stretch, then to the recent peak") {
    GpuShareEstimate e;
    e.note(0.006);
    for (int i = 0; i < GpuShareEstimate::kCleanFrames - 1; ++i) {
        e.note(0.0);
    }
    CHECK(e.seconds() == doctest::Approx(0.006)); // one frame short of the stretch
    e.note(0.0);
    CHECK(e.seconds() == 0.0);
    // A wait between clean frames starts the stretch over and holds the estimate up.
    e.note(0.003);
    for (int i = 0; i < 10; ++i) {
        e.note(0.0);
    }
    e.note(0.002);
    for (int i = 0; i < GpuShareEstimate::kCleanFrames - 1; ++i) {
        e.note(0.0);
    }
    CHECK(e.seconds() == doctest::Approx(0.003));
    e.note(0.0);
    CHECK(e.seconds() == 0.0);
}

TEST_CASE("a GPU-bound stretch keeps the estimate at its peak; nonsense is ignored") {
    GpuShareEstimate e;
    for (int i = 0; i < 200; ++i) {
        e.note(i % 3 == 0 ? 0.0 : 0.002 + 0.00002 * (i % 50)); // waits most frames, never 30 clean in a row
    }
    CHECK(e.seconds() >= 0.002);
    CHECK(e.seconds() <= 0.003);
    const double before = e.seconds();
    e.note(std::numeric_limits<double>::quiet_NaN());
    e.note(-1.0);
    e.note(std::numeric_limits<double>::infinity());
    CHECK(e.seconds() == before);
}
