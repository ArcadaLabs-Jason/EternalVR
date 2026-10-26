#include "features/menu/pointer_steady.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::input::aimSmoothingParams;
using evr::input::Hand;
using evr::input::kDefaultAimSmoothing;
using evr::menu::PointerSteady;

namespace {

constexpr double kDt = 1.0 / 90.0;
constexpr float kDegrees = std::numbers::pi_v<float> / 180.0f;

Pose aimAt(float yawDegrees, Vec3 position = {0.1f, 1.2f, -0.3f}) {
    return {Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yawDegrees * kDegrees), position};
}

float yawDegrees(const Pose& p) {
    const Vec3 f = rotate(p.orientation, {0.0f, 0.0f, -1.0f});
    return std::atan2(-f.x, -f.z) / kDegrees;
}

// A hand held out with a 0.5 degree, 8 Hz tremor (the shake of an unsupported arm) for 2 s: the largest yaw
// the pointer shows after the first 0.5 s.
float tremorShown(PointerSteady& steady, Hand hand) {
    float worst = 0.0f;
    for (int i = 0; i < 180; ++i) {
        const double t = i * kDt;
        const float shake = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 8.0 * t));
        const Pose shown = steady.update(hand, aimAt(shake), t);
        if (t >= 0.5) {
            worst = std::max(worst, std::fabs(yawDegrees(shown)));
        }
    }
    return worst;
}

} // namespace

TEST_CASE("the pointer takes the gun's steadiness: a shaking hand moves it under half as much") {
    PointerSteady off(std::nullopt);
    PointerSteady steady(aimSmoothingParams(kDefaultAimSmoothing));
    const float raw = tremorShown(off, Hand::Right);
    const float shown = tremorShown(steady, Hand::Right);
    CHECK(raw == doctest::Approx(0.5f).epsilon(0.02));
    CHECK(shown < 0.5f * raw);
}

TEST_CASE("off passes the pose through; only the orientation is filtered, never the position") {
    PointerSteady off(std::nullopt);
    const Pose a = aimAt(10.0f);
    const Pose shownOff = off.update(Hand::Left, a, 0.0);
    CHECK(yawDegrees(shownOff) == doctest::Approx(10.0f));

    PointerSteady steady(aimSmoothingParams(1.0f));
    steady.update(Hand::Left, aimAt(0.0f), 0.0);
    const Pose moved = aimAt(20.0f, {0.4f, 1.0f, -0.2f});
    const Pose shown = steady.update(Hand::Left, moved, kDt);
    CHECK(shown.position.x == doctest::Approx(0.4f));
    CHECK(shown.position.y == doctest::Approx(1.0f));
    CHECK(shown.position.z == doctest::Approx(-0.2f));
    CHECK(yawDegrees(shown) < 20.0f); // the turn is followed, a little later
}

TEST_CASE("a deliberate turn is followed: the pointer reaches a new menu item within a quarter second") {
    PointerSteady steady(aimSmoothingParams(kDefaultAimSmoothing));
    double t = 0.0;
    for (int i = 0; i < 45; ++i, t += kDt) {
        steady.update(Hand::Right, aimAt(0.0f), t);
    }
    Pose shown;
    for (int i = 0; i < 23; ++i, t += kDt) { // 0.25 s at the new item, 15 degrees over
        shown = steady.update(Hand::Right, aimAt(15.0f), t);
    }
    CHECK(yawDegrees(shown) > 14.0f);
}

TEST_CASE("each hand has its own filter, and a reset hand's next pose passes through") {
    PointerSteady steady(aimSmoothingParams(1.0f));
    steady.update(Hand::Left, aimAt(0.0f), 0.0);
    steady.update(Hand::Right, aimAt(0.0f), 0.0);
    // The right hand jumps: the left hand's pointer is not pulled along.
    steady.update(Hand::Right, aimAt(30.0f), kDt);
    CHECK(yawDegrees(steady.update(Hand::Left, aimAt(0.0f), kDt)) == doctest::Approx(0.0f).epsilon(0.001));
    // The same time again gives the same pose.
    const float once = yawDegrees(steady.update(Hand::Right, aimAt(30.0f), 2 * kDt));
    CHECK(yawDegrees(steady.update(Hand::Right, aimAt(45.0f), 2 * kDt)) == doctest::Approx(once));
    steady.reset(Hand::Right);
    CHECK(yawDegrees(steady.update(Hand::Right, aimAt(60.0f), 3 * kDt)) == doctest::Approx(60.0f));
}
