#include "stereo_seq/eye_tags.hpp"
#include "stereo_seq/render_idle.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using evr::stereo_seq::Eye;
using evr::stereo_seq::EyeTagQueue;
using evr::stereo_seq::FrameEndInput;
using evr::stereo_seq::kUnverifiedIdleQuietMs;
using evr::stereo_seq::planFrameEnd;
using evr::stereo_seq::RenderIdle;
using evr::stereo_seq::RenderTag;

using Verdict = RenderIdle::Verdict;

TEST_CASE("render idle: without a reference only a long quiet period counts as idle") {
    RenderIdle idle;
    idle.kicked();
    CHECK_FALSE(idle.hasReference());
    CHECK(idle.check(100, 0) == Verdict::Wait);
    CHECK(idle.check(100, 30) == Verdict::Wait); // a 30 ms wait is not enough: a slow frame can take longer
    CHECK(idle.check(100, kUnverifiedIdleQuietMs) == Verdict::IdleUnverified);
}

TEST_CASE("render idle: with a reference the frame counts decide, not the quiet time") {
    RenderIdle idle;
    idle.based(500);
    CHECK(idle.check(500, 0) == Verdict::Idle);
    idle.kicked();                                 // eye L
    idle.kicked();                                 // eye R
    CHECK(idle.check(500, 1000) == Verdict::Wait); // two frames handed over, none presented: never idle
    CHECK(idle.check(501, 1000) == Verdict::Wait);
    CHECK(idle.check(502, 0) == Verdict::Idle); // both presented: idle at once
}

TEST_CASE("render idle: a slow frame keeps the base from being taken early (the shifted pair)") {
    // Tags based while a kicked frame is still on the render thread would give that frame eye L's tag and
    // eye L's image eye R's: a pair made of two different frames.
    RenderIdle idle;
    idle.based(10);
    idle.kicked(); // a mono frame whose backend work takes 60 ms
    CHECK(idle.check(10, 40) == Verdict::Wait);
    CHECK(idle.check(10, 200) == Verdict::Wait);
    CHECK(idle.check(11, 0) == Verdict::Idle);

    EyeTagQueue tags;
    tags.rebase(11);
    RenderTag left;
    left.eye = Eye::Left;
    left.tick = 7;
    left.viewApplied = true;
    REQUIRE(tags.push(left));
    CHECK_FALSE(tags.pop(11).tagged); // the slow frame's present, if it comes late, stays untagged
    const auto match = tags.pop(12);
    REQUIRE(match.tagged);
    CHECK(match.tag.eye == Eye::Left);
}

TEST_CASE("render idle: backend frames nobody counted drop back to the quiet period") {
    RenderIdle idle;
    idle.based(10);
    idle.kicked();
    CHECK(idle.check(12, 0) == Verdict::Wait); // ahead of the count: it tells nothing
    CHECK(idle.check(12, kUnverifiedIdleQuietMs) == Verdict::IdleUnverified);
    idle.based(12);
    CHECK(idle.check(12, 0) == Verdict::Idle);
}

TEST_CASE("render idle: forgetting the reference after a timeout") {
    RenderIdle idle;
    idle.based(10);
    idle.kicked(); // a frame that never presents
    CHECK(idle.check(10, 250) == Verdict::Wait);
    idle.forget();
    CHECK(idle.check(10, kUnverifiedIdleQuietMs) == Verdict::IdleUnverified);
}

TEST_CASE("render idle: counters wrap") {
    RenderIdle idle;
    idle.based(0xFFFFFFFFu);
    idle.kicked();
    idle.kicked();
    CHECK(idle.check(0xFFFFFFFFu, 0) == Verdict::Wait);
    CHECK(idle.check(1u, 0) == Verdict::Idle);
}

TEST_CASE("frame end: no present, nothing to do") {
    FrameEndInput in;
    in.leftApplied = true;
    in.wanted = true;
    in.drainAllowed = true;
    const auto plan = planFrameEnd(in);
    CHECK_FALSE(plan.drain);
    CHECK_FALSE(plan.stereo);
}

TEST_CASE("frame end: steady stereo pairs without a drain") {
    FrameEndInput in;
    in.presents = true;
    in.leftApplied = true;
    in.synced = true;
    const auto plan = planFrameEnd(in);
    CHECK(plan.stereo);
    CHECK_FALSE(plan.drain);
}

TEST_CASE("frame end: a wanted tick drains on a mono frame, never on one drawn for eye L") {
    FrameEndInput in;
    in.presents = true;
    in.wanted = true;
    in.drainAllowed = true;
    auto plan = planFrameEnd(in); // tags not in step yet
    CHECK(plan.drain);
    CHECK_FALSE(plan.stereo); // the game's own view: shown mono, the next tick pairs
    in.drainAllowed = false;
    plan = planFrameEnd(in);
    CHECK_FALSE(plan.drain);
    in.synced = true;
    in.drainAllowed = true;
    in.rebaseDue = true;
    CHECK(planFrameEnd(in).drain);
    in.rebaseDue = false;
    CHECK_FALSE(planFrameEnd(in).drain); // in step and no fresh base due
}

TEST_CASE("frame end: eye L drawn while the tags fell out of step drains whatever the spacing") {
    FrameEndInput in;
    in.presents = true;
    in.leftApplied = true;
    in.synced = false;
    in.drainAllowed = false;
    const auto plan = planFrameEnd(in);
    CHECK(plan.drain);
    CHECK(plan.stereo); // if the base is taken
}

TEST_CASE("frame end: a mono frame nobody wanted stereo for never drains") {
    FrameEndInput in;
    in.presents = true;
    in.drainAllowed = true;
    const auto plan = planFrameEnd(in);
    CHECK_FALSE(plan.drain);
    CHECK_FALSE(plan.stereo);
}
