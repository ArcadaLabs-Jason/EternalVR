#include "features/tracking/pose_guard.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <ostream>

using evr::Vec3;
using evr::tracking::GuardStep;
using evr::tracking::kHandLimits;
using evr::tracking::kHeadLimits;
using evr::tracking::kSettleSeconds;
using evr::tracking::PoseGuard;

namespace {

constexpr double kFrame = 1.0 / 90.0;
const Vec3 kStanding{0.0f, 1.7f, 0.0f};

bool samePlace(Vec3 a, Vec3 b) {
    return evr::length(a - b) < 1e-4f;
}

} // namespace

TEST_CASE("a head moving as heads do passes untouched") {
    PoseGuard guard(kHeadLimits);
    Vec3 p = kStanding;
    for (int i = 0; i < 270; ++i) {
        p.x += 2.5f * static_cast<float>(kFrame); // a fast lean, 2.5 m/s
        const GuardStep step = guard.update(p, i * kFrame);
        CHECK_FALSE(step.held);
        CHECK(samePlace(step.position, p));
    }
    // A duck of 0.3 m in one game frame at 30 fps is within the limits too.
    CHECK_FALSE(guard.update(p - Vec3{0.0f, 0.3f, 0.0f}, 270 * kFrame + 1.0 / 30.0).held);
}

TEST_CASE("a jump no head makes is held, and the real position after it is taken") {
    PoseGuard guard(kHeadLimits);
    CHECK_FALSE(guard.update(kStanding, 0.0).held);
    // A WMR glitch: 9.9 m away for three frames.
    const Vec3 far{9.9f, 1.7f, 0.0f};
    for (int i = 1; i <= 3; ++i) {
        const GuardStep step = guard.update(far, i * kFrame);
        CHECK(step.held);
        CHECK(samePlace(step.position, kStanding));
        CHECK(step.jumpMetres == doctest::Approx(9.9f));
        CHECK(guard.holding());
    }
    const GuardStep back = guard.update(kStanding + Vec3{0.01f, 0.0f, 0.0f}, 4 * kFrame);
    CHECK_FALSE(back.held);
    CHECK(back.released);
    CHECK(back.heldSeconds == doctest::Approx(3 * kFrame));
    CHECK_FALSE(guard.holding());
}

TEST_CASE("a new place that holds still is taken after the settle time") {
    PoseGuard guard(kHeadLimits);
    guard.update(kStanding, 0.0);
    const Vec3 moved{3.0f, 1.7f, 2.0f}; // a teleport no event announced
    double t = kFrame;
    GuardStep step;
    for (; t - kFrame < kSettleSeconds; t += kFrame) {
        step = guard.update(moved, t);
        CHECK(step.held);
    }
    step = guard.update(moved, t);
    CHECK(step.taken);
    CHECK_FALSE(step.held);
    CHECK(samePlace(step.position, moved));
    CHECK_FALSE(guard.update(moved + Vec3{0.01f, 0.0f, 0.0f}, t + kFrame).held);
}

TEST_CASE("garbage that jumps about is never taken as a place; the reach grows so no hold lasts") {
    PoseGuard guard(kHeadLimits);
    guard.update(kStanding, 0.0);
    double t = kFrame;
    GuardStep step;
    for (int i = 0; t < 1.5; ++i, t += kFrame) {
        step = guard.update(i % 2 == 0 ? Vec3{9.9f, 1.7f, 0.0f} : Vec3{-12.7f, 1.7f, 3.0f}, t);
        REQUIRE(step.held);
        REQUIRE_FALSE(step.taken);
    }
    // 0.25 m + 6 m/s x 1.7 s reaches 9.9 m: within reach, taken as an ordinary move.
    step = guard.update(Vec3{9.9f, 1.7f, 0.0f}, 1.7);
    CHECK_FALSE(step.held);
    CHECK(step.released);
}

TEST_CASE("a time a few milliseconds back is the same clock; further back is a new one") {
    PoseGuard guard(kHeadLimits);
    guard.update(kStanding, 10.0);
    const Vec3 far{5.0f, 1.7f, 0.0f};
    CHECK(guard.update(far, 10.0 + kFrame).held);
    CHECK(guard.update(far, 10.0 + kFrame - 0.003).held); // a pose time a little back: still held
    CHECK_FALSE(guard.update(kStanding, 10.0 - 0.002).held);
    CHECK(guard.update(far, 10.0 + 2 * kFrame).held);
    CHECK_FALSE(guard.update(far, 9.0).held); // a new clock: taken
}

TEST_CASE("reset takes the next position at once; time going back starts over too") {
    PoseGuard guard(kHeadLimits);
    guard.update(kStanding, 10.0);
    CHECK(guard.update(Vec3{5.0f, 1.7f, 0.0f}, 10.0 + kFrame).held);
    guard.reset();
    CHECK_FALSE(guard.update(Vec3{5.0f, 1.7f, 0.0f}, 10.0 + 2 * kFrame).held);
    CHECK_FALSE(guard.update(kStanding, 1.0).held); // a new session's clock
    CHECK_FALSE(guard.holding());
}

TEST_CASE("a position or time that is not finite is passed through") {
    PoseGuard guard(kHeadLimits);
    guard.update(kStanding, 0.0);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const GuardStep step = guard.update(Vec3{nan, 1.7f, 0.0f}, kFrame);
    CHECK_FALSE(step.held);
    CHECK_FALSE(guard.update(Vec3{0.0f, 1.7f, 0.0f}, std::numeric_limits<double>::infinity()).held);
}

TEST_CASE("hands are allowed a punch, not a teleport; their velocity has a limit") {
    PoseGuard hand(kHandLimits);
    Vec3 p{0.2f, 1.2f, -0.2f};
    hand.update(p, 0.0);
    for (int i = 1; i <= 10; ++i) {
        p.z -= 12.0f * static_cast<float>(kFrame); // 12 m/s
        CHECK_FALSE(hand.update(p, i * kFrame).held);
    }
    CHECK(hand.update(p + Vec3{0.0f, 0.0f, -12.7f}, 11 * kFrame).held);
    CHECK(evr::tracking::plausibleHandVelocity(Vec3{0.0f, 0.0f, -12.0f}));
    CHECK_FALSE(evr::tracking::plausibleHandVelocity(Vec3{0.0f, 0.0f, -40.0f}));
    CHECK_FALSE(
        evr::tracking::plausibleHandVelocity(Vec3{std::numeric_limits<float>::infinity(), 0.0f, 0.0f}));
}
