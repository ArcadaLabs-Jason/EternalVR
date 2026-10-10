#include "vkcore/view_dlss_plan.hpp"

#include <doctest/doctest.h>

using evr::vkcore::view_dlss::Health;
using evr::vkcore::view_dlss::kFailuresInARow;
using evr::vkcore::view_dlss::kRecoveredEvaluates;
using evr::vkcore::view_dlss::ResetPlan;
using evr::vkcore::view_dlss::Verdict;

TEST_CASE("view DLSS resets: none while both views render every frame") {
    ResetPlan plan;
    for (int i = 0; i < 100; ++i) {
        CHECK_FALSE(plan.evaluate(i % 2, false, true));
        CHECK_FALSE(plan.evaluate(1 - i % 2, false, true));
    }
}

TEST_CASE("view DLSS resets: view 1 starts again after frames without it, view 0 does not") {
    ResetPlan plan;
    // A loading screen or new clones: the frames sent lately did not all render view 1.
    CHECK(plan.evaluate(1, false, false));
    CHECK(plan.evaluate(1, false, false));
    CHECK_FALSE(plan.evaluate(0, false, false)); // view 0 rendered every frame
    CHECK_FALSE(plan.evaluate(1, false, true));  // the run is back
}

TEST_CASE("view DLSS resets: an engine reset that reached one view is given to the other's next evaluation") {
    ResetPlan plan;
    // The engine raised it for view 0 (r_dlssForceReset, counted down by one evaluation): nothing to add.
    CHECK_FALSE(plan.evaluate(0, true, true));
    CHECK(plan.evaluate(1, false, true));       // view 1's next evaluation resets too
    CHECK_FALSE(plan.evaluate(1, false, true)); // once
    CHECK_FALSE(plan.evaluate(0, false, true));
    // The other way round.
    CHECK_FALSE(plan.evaluate(1, true, true));
    CHECK(plan.evaluate(0, false, true));
    CHECK_FALSE(plan.evaluate(0, false, true));
}

TEST_CASE("view DLSS resets: an owed reset the engine raises itself is not raised again, nor given back") {
    ResetPlan plan;
    CHECK_FALSE(plan.evaluate(0, true, true));
    CHECK_FALSE(plan.evaluate(1, true, true)); // the engine's own, the one view 0's gave: nothing owed back
    CHECK_FALSE(plan.evaluate(0, false, true));
    CHECK_FALSE(plan.evaluate(1, false, true));
    // A countdown of three: view 0, view 1, view 0 again; view 1 is owed the last one.
    CHECK_FALSE(plan.evaluate(0, true, true));
    CHECK_FALSE(plan.evaluate(1, true, true));
    CHECK_FALSE(plan.evaluate(0, true, true));
    CHECK(plan.evaluate(1, false, true));
    CHECK_FALSE(plan.evaluate(0, false, true));
}

TEST_CASE("view DLSS health: a view's failures in a row fall back once, a success clears them") {
    Health h;
    for (int i = 1; i < kFailuresInARow; ++i) {
        CHECK(h.result(1, false) == Verdict::None);
    }
    CHECK(h.result(1, true) == Verdict::None); // the run is broken
    CHECK(h.failures(1) == 0);
    for (int i = 1; i < kFailuresInARow; ++i) {
        CHECK(h.result(1, false) == Verdict::None);
        CHECK(h.result(0, true) == Verdict::None); // the other view's successes do not clear it
    }
    CHECK(h.result(1, false) == Verdict::Failed);
    // Evaluations recorded before the fallback takes hold: no second verdict from the same run.
    CHECK(h.result(1, false) == Verdict::None);
    CHECK(h.failures(1) == kFailuresInARow + 1);
}

TEST_CASE("view DLSS health: either view's run falls back") {
    Health h;
    for (int i = 1; i < kFailuresInARow; ++i) {
        CHECK(h.result(0, false) == Verdict::None);
    }
    CHECK(h.result(0, false) == Verdict::Failed);
}

TEST_CASE("view DLSS health: no recovery at the start, one after a try once both views run") {
    Health h;
    for (int i = 0; i < 2 * kRecoveredEvaluates; ++i) {
        CHECK(h.result(0, true) == Verdict::None);
        CHECK(h.result(1, true) == Verdict::None);
    }
    h.tryAgain();
    CHECK(h.failures(0) == 0);
    for (int i = 1; i < kRecoveredEvaluates; ++i) {
        CHECK(h.result(1, true) == Verdict::None);
        CHECK(h.result(0, true) == Verdict::None);
    }
    CHECK(h.result(1, true) == Verdict::None); // view 1 at its count, view 0 one short
    CHECK(h.result(0, true) == Verdict::Recovered);
    CHECK(h.result(1, true) == Verdict::None); // once
    CHECK(h.result(0, true) == Verdict::None);
}

TEST_CASE("view DLSS health: a try recovers with view 0 alone when view 1 stopped") {
    Health h;
    for (int i = 0; i < kFailuresInARow; ++i) {
        h.result(1, false);
    }
    h.tryAgain();
    CHECK(h.result(1, true) == Verdict::None); // one evaluation, then view 1 stops (a guard trip)
    for (int i = 1; i < kRecoveredEvaluates; ++i) {
        CHECK(h.result(0, true) == Verdict::None);
    }
    CHECK(h.result(0, true) == Verdict::Recovered);
    // Interleaved, view 0's run alone never counts: view 1 has to reach its own.
    Health both;
    both.tryAgain();
    for (int i = 1; i < kRecoveredEvaluates; ++i) {
        CHECK(both.result(0, true) == Verdict::None);
        CHECK(both.result(1, true) == Verdict::None);
    }
    CHECK(both.result(0, true) == Verdict::None); // view 0 at its count, view 1 one short
    CHECK(both.result(1, true) == Verdict::Recovered);
}

TEST_CASE("view DLSS health: a try that fails falls back again") {
    Health h;
    for (int i = 0; i < kFailuresInARow; ++i) {
        h.result(1, false);
    }
    h.tryAgain();
    for (int i = 1; i < kFailuresInARow; ++i) {
        CHECK(h.result(1, false) == Verdict::None);
    }
    CHECK(h.result(1, false) == Verdict::Failed);
}
