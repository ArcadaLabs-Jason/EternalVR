#pragma once

// Parallel Eye Rendering with DLSS (view_dlss.hpp): when the layer raises DLSS's "Reset" for a view and when
// DLSS falls back to TAA. Header-only, without the game (tests/vkcore/view_dlss_plan_tests.cpp).

namespace evr::vkcore::view_dlss {

// Failures in a row of one view's evaluations that make DLSS fall back to TAA in both eyes: the engine's own
// count (0x1C9B8B6), which counts both views together and so may never reach it.
inline constexpr int kFailuresInARow = 3;
// Successful evaluations in a row of each view that end a try after a fallback (DLSS in both eyes again).
inline constexpr int kRecoveredEvaluates = 30;
// Tries after a fallback the layer starts by itself in a session; a choice of DLSS in the game's video menu
// tries again whatever the count. Each switch between DLSS and TAA changes the render size, which makes view
// 1's clones again (view_clones.cpp), and a session has a few clone builds only before view 1 stops.
inline constexpr int kAutomaticTries = 1;

// "Reset" for each view's evaluation. Each view's feature keeps its own history, so a view that missed frames
// (view 0 alone: a loading screen, the async compute safety net, new clones) starts again from its next
// evaluations, and an engine reset (r_dlssForceReset, one countdown for both views' evaluations together)
// that reached one view is given to the other view's next evaluation too (not back again when that one was
// the engine's own as well: the countdown gave both views theirs).
class ResetPlan {
public:
    // `view`'s evaluation is about to record. `engineReset`: the engine raised Reset for it. `view1Current`:
    // the frames sent lately all rendered view 1 (view_frames.hpp). True when the layer raises Reset for it
    // (false when the engine's own is raised already).
    bool evaluate(int view, bool engineReset, bool view1Current) {
        const int v = view == 1 ? 1 : 0;
        const bool owed = owed_[v];
        bool raise = owed;
        owed_[v] = false;
        if (v == 1 && !view1Current) {
            raise = true;
        }
        if (engineReset && !owed) {
            owed_[1 - v] = true;
        }
        return raise && !engineReset;
    }

private:
    bool owed_[2] = {};
};

enum class Verdict {
    None,
    Failed, // a view failed kFailuresInARow times in a row: TAA in both eyes until a try
    // After a try, both views evaluated kRecoveredEvaluates times in a row, or view 0 did with no evaluation
    // of view 1 in between (view 1 stopped: a guard trip, the clones off).
    Recovered,
};

// Each view's evaluation results.
class Health {
public:
    // `view`'s evaluation returned `ok`.
    Verdict result(int view, bool ok) {
        const int v = view == 1 ? 1 : 0;
        if (v == 1) {
            view0Alone_ = 0;
        }
        if (!ok) {
            successes_[v] = 0;
            // Once per run: the evaluations recorded before the fallback takes hold do not count again.
            return ++failures_[v] == kFailuresInARow ? Verdict::Failed : Verdict::None;
        }
        failures_[v] = 0;
        if (successes_[v] < kRecoveredEvaluates) {
            ++successes_[v];
        }
        if (v == 0 && view0Alone_ < kRecoveredEvaluates) {
            ++view0Alone_;
        }
        const bool view1Done = successes_[1] >= kRecoveredEvaluates || view0Alone_ >= kRecoveredEvaluates;
        if (trying_ && successes_[0] >= kRecoveredEvaluates && view1Done) {
            trying_ = false;
            return Verdict::Recovered;
        }
        return Verdict::None;
    }

    // DLSS is held again after a fallback: the counts start over, and a recovery is reported once.
    void tryAgain() {
        failures_[0] = failures_[1] = 0;
        successes_[0] = successes_[1] = 0;
        view0Alone_ = 0;
        trying_ = true;
    }

    int failures(int view) const { return failures_[view == 1 ? 1 : 0]; }

private:
    int failures_[2] = {};
    int successes_[2] = {};
    int view0Alone_ = 0; // view 0's successes in a row since view 1's last evaluation
    bool trying_ = false;
};

} // namespace evr::vkcore::view_dlss
