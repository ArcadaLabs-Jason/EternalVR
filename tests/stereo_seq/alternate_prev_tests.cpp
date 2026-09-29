#include "stereo_seq/alternate_prev.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <random>
#include <vector>

using evr::stereo_seq::planPrevious;
using evr::stereo_seq::PrevAction;
using evr::stereo_seq::PrevHold;
using evr::stereo_seq::PrevPlan;

namespace {

// One entity as the commit hook sees it: numbers stand in for its matrices. A commit copies the current
// matrix over the previous one, the hook acts on the plan, then the commit sets a new current matrix.
struct Entity {
    int current = 0;
    int previous = 0;
    PrevHold hold;
    int held = 0;           // the matrix the hook keeps with the hold
    std::vector<int> endOf; // the current matrix at the end of each render

    void commit(std::uint64_t render, int next) {
        const PrevPlan plan = planPrevious(hold, render);
        const int before = previous; // what the hook saves for Keep
        const int heldBefore = held; // and for Restore
        if (plan.hold) {
            hold = PrevHold{true, render};
            held = current;
        }
        previous = current; // the engine's copy
        current = next;     // the engine's call
        if (plan.action == PrevAction::Keep) {
            previous = before;
        } else if (plan.action == PrevAction::Restore) {
            previous = heldBefore;
        }
    }
};

} // namespace

TEST_CASE("alternate previous matrix: the plan for each commit") {
    CHECK(planPrevious(PrevHold{}, 10).action == PrevAction::Engine);
    CHECK(planPrevious(PrevHold{}, 10).hold);
    CHECK(planPrevious(PrevHold{true, 8}, 10).action == PrevAction::Engine);
    CHECK(planPrevious(PrevHold{true, 9}, 10).action == PrevAction::Restore);
    CHECK(planPrevious(PrevHold{true, 9}, 10).hold);
    CHECK(planPrevious(PrevHold{true, 10}, 10).action == PrevAction::Keep);
    CHECK_FALSE(planPrevious(PrevHold{true, 10}, 10).hold);
    CHECK(planPrevious(PrevHold{true, 12}, 10).action == PrevAction::Engine); // out of order
}

TEST_CASE("alternate previous matrix: an entity moving every tick gets the matrix of two renders back") {
    Entity e;
    int value = 100;
    for (std::uint64_t render = 1; render < 40; ++render) {
        e.commit(render, ++value);
        if (render >= 3) {
            // Two renders back is this eye's last render (the order is L, R, L, R).
            CHECK(e.previous == value - 2);
        }
    }
}

TEST_CASE("alternate previous matrix: any pattern of commits leaves the end of render n - 2") {
    std::mt19937 rng(7);
    for (int run = 0; run < 200; ++run) {
        Entity e;
        int value = 0;
        e.endOf.push_back(0); // render 0: before any commit
        for (std::uint64_t render = 1; render < 60; ++render) {
            // Not committed, committed once, or twice in one render (a recommit).
            const int commits = static_cast<int>(rng() % 3u);
            for (int c = 0; c < commits; ++c) {
                e.commit(render, ++value);
                if (render >= 2) {
                    CHECK(e.previous == e.endOf[render - 2]);
                }
            }
            e.endOf.push_back(e.current);
        }
    }
}

TEST_CASE("alternate previous matrix: under Route S's nested eye R it is keep_prev's answer") {
    // Eye L of tick t commits (render 2t), eye R's recommit of the same tick (render 2t + 1) keeps the
    // previous matrix eye L's commit left: the current matrix of tick t - 1.
    Entity e;
    int value = 0;
    for (std::uint64_t tick = 1; tick < 20; ++tick) {
        const int before = e.current;
        e.commit(tick * 2, ++value); // eye L: the game moved it
        CHECK(e.previous == before); // the engine's copy: tick t - 1
        const int left = e.current;
        e.commit(tick * 2 + 1, left); // eye R recommits the same pose
        CHECK(e.previous == before);  // still tick t - 1, not eye L's current
        CHECK(e.current == left);
    }
}
