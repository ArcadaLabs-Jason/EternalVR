#include "xr_math/aim_check.hpp"

#include <doctest/doctest.h>

using evr::xr_math::AimCheck;
using evr::xr_math::IdAngles;

namespace {

constexpr IdAngles kCommand{10.0f, 30.0f, 0.0f};
constexpr IdAngles kDelta{-2.0f, 5.0f, 0.0f};
constexpr IdAngles kStateDelta{1.0f, -40.0f, 0.0f};
constexpr IdAngles kPhysicsView{8.0f, 35.0f, 0.0f}; // command + physics delta
constexpr IdAngles kStateView{11.0f, -10.0f, 0.0f}; // command + state delta
constexpr IdAngles kOtherView{0.0f, 120.0f, 0.0f};  // neither (a scripted camera's angles)

AimCheck::Step frame(AimCheck& check, const IdAngles& view, bool driven = false) {
    return check.update(view, kCommand, kDelta, kStateDelta, driven);
}

// Runs one try to its verdict with `matching` frames of `view` and the rest matching neither.
AimCheck::Step runTry(AimCheck& check, const IdAngles& view, int matching) {
    AimCheck::Step step;
    for (int i = 0; i < AimCheck::kFrames; ++i) {
        step = frame(check, i < matching ? view : kOtherView);
    }
    return step;
}

// Plays the frames between tries; returns the event of the frame that ended the wait.
AimCheck::Event settle(AimCheck& check) {
    const int frames = check.settleFrames();
    for (int i = 1; i < frames; ++i) {
        REQUIRE(frame(check, kPhysicsView).event == AimCheck::Event::Waiting);
    }
    return frame(check, kPhysicsView).event;
}

} // namespace

TEST_CASE("aim check: 54 of 60 frames through the physics delta pass") {
    AimCheck check;
    for (int i = 1; i < AimCheck::kFrames; ++i) {
        CHECK(frame(check, i <= 6 ? kOtherView : kPhysicsView).event == AimCheck::Event::Counted);
    }
    const AimCheck::Step last = frame(check, kPhysicsView);
    CHECK(last.event == AimCheck::Event::Passed);
    CHECK(last.counted);
    CHECK(last.physicsMatch);
    CHECK(check.passed());
    CHECK(check.physics());
    CHECK(check.physicsMatches() == 54);
    CHECK(check.mismatches() == 6);
    CHECK(frame(check, kOtherView).event == AimCheck::Event::Over);
}

TEST_CASE("aim check: the state delta passes when the physics one does not") {
    AimCheck check;
    CHECK(runTry(check, kStateView, 60).event == AimCheck::Event::Passed);
    CHECK_FALSE(check.physics());
    CHECK(check.stateMatches() == 60);
    CHECK(check.physicsMatches() == 0);
}

TEST_CASE("aim check: driven frames are not counted") {
    // A level's opening cutscene during the check (issue 7: 51 of 60 through the state delta).
    AimCheck check;
    for (int i = 0; i < 30; ++i) {
        CHECK(frame(check, kStateView).event == AimCheck::Event::Counted);
    }
    for (int i = 0; i < 200; ++i) {
        const AimCheck::Step skipped = frame(check, kOtherView, true);
        CHECK(skipped.event == AimCheck::Event::Skipped);
        CHECK_FALSE(skipped.counted);
    }
    CHECK(check.checks() == 30);
    CHECK(check.skipped() == 200);
    for (int i = 0; i < 29; ++i) {
        CHECK(frame(check, kStateView).event == AimCheck::Event::Counted);
    }
    CHECK(frame(check, kStateView).event == AimCheck::Event::Passed);
    CHECK(check.stateMatches() == 60);
}

TEST_CASE("aim check: a failed try runs again once the player has had their own view") {
    AimCheck check;
    const AimCheck::Step failed = runTry(check, kStateView, 51);
    CHECK(failed.event == AimCheck::Event::Failed);
    CHECK_FALSE(check.passed());
    CHECK_FALSE(check.gaveUp());
    CHECK(check.mismatches() == 9);
    // A driven frame starts the wait again.
    for (int i = 0; i < 100; ++i) {
        CHECK(frame(check, kPhysicsView).event == AimCheck::Event::Waiting);
    }
    CHECK(frame(check, kPhysicsView, true).event == AimCheck::Event::Waiting);
    CHECK(settle(check) == AimCheck::Event::Retry);
    CHECK(check.tryNumber() == 2);
    CHECK(check.checks() == 0);
    CHECK(check.skipped() == 0);
    CHECK(runTry(check, kStateView, 60).event == AimCheck::Event::Passed);
    CHECK_FALSE(check.physics());
}

TEST_CASE("aim check: gives up after the last try, waiting longer before each") {
    AimCheck check;
    for (int retry = 0; retry < AimCheck::kRetries; ++retry) {
        CHECK(check.settleFrames() == AimCheck::kSettleFrames * (retry + 1));
        CHECK(runTry(check, kOtherView, 0).event == AimCheck::Event::Failed);
        CHECK(settle(check) == AimCheck::Event::Retry);
    }
    CHECK(check.tryNumber() == AimCheck::kRetries + 1);
    CHECK(runTry(check, kOtherView, 0).event == AimCheck::Event::GaveUp);
    CHECK(check.gaveUp());
    CHECK(frame(check, kPhysicsView).event == AimCheck::Event::Over);
}

TEST_CASE("aim check: yaw is compared across the wrap at 180 degrees") {
    AimCheck check;
    const IdAngles command{0.0f, 170.0f, 0.0f};
    const IdAngles delta{0.0f, 20.0f, 0.0f};
    const IdAngles view{0.0f, -170.0f, 0.0f};
    const AimCheck::Step step = check.update(view, command, delta, kStateDelta, false);
    CHECK(step.physicsMatch);
    CHECK_FALSE(step.stateMatch);
}
