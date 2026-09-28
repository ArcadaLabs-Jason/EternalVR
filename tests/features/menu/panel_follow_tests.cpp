#include "features/menu/panel_follow.hpp"

#include "common/quat.hpp"

#include <doctest/doctest.h>

#include <cmath>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::menu::horizontalAngleTo;
using evr::menu::PanelFollow;
using evr::menu::PanelFollowTuning;

namespace {

constexpr float kPi = 3.14159265358979f;

Pose headTurned(float yawDegrees, float pitchDegrees = 0.0f) {
    Pose head;
    head.position = {0.0f, 1.7f, 0.0f};
    head.orientation = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yawDegrees * kPi / 180.0f) *
                       Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, pitchDegrees * kPi / 180.0f);
    return head;
}

const Vec3 kAhead{0.0f, 1.7f, -1.5f}; // a panel placed in front of the unturned head

} // namespace

TEST_CASE("the horizontal angle to the panel follows the head's yaw, not its pitch or height") {
    CHECK(horizontalAngleTo(headTurned(0.0f), kAhead) == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(horizontalAngleTo(headTurned(90.0f), kAhead) == doctest::Approx(90.0f).epsilon(0.001));
    CHECK(horizontalAngleTo(headTurned(-90.0f), kAhead) == doctest::Approx(90.0f).epsilon(0.001));
    CHECK(horizontalAngleTo(headTurned(180.0f), kAhead) == doctest::Approx(180.0f).epsilon(0.001));
    CHECK(horizontalAngleTo(headTurned(0.0f, -40.0f), kAhead) == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(horizontalAngleTo(headTurned(30.0f), Vec3{0.0f, 3.0f, -1.5f}) ==
          doctest::Approx(30.0f).epsilon(0.001));
}

TEST_CASE("a head looking straight down or a panel overhead gives no angle") {
    CHECK(horizontalAngleTo(headTurned(0.0f, -90.0f), kAhead) == 0.0f);
    CHECK(horizontalAngleTo(headTurned(45.0f), Vec3{0.0f, 3.0f, 0.0f}) == 0.0f);
}

TEST_CASE("the panel comes back only after the head has looked away long enough") {
    PanelFollow follow;
    CHECK_FALSE(follow.update(20.0f, 0.0));
    CHECK_FALSE(follow.update(90.0f, 1.0));
    CHECK_FALSE(follow.update(90.0f, 1.9));
    CHECK(follow.update(90.0f, 2.0));
    CHECK_FALSE(follow.update(90.0f, 2.1)); // the wait starts over
    CHECK(follow.update(90.0f, 3.1));
}

TEST_CASE("a glance away shorter than the wait leaves the panel where it is") {
    PanelFollow follow;
    CHECK_FALSE(follow.update(90.0f, 0.0));
    CHECK_FALSE(follow.update(90.0f, 0.8));
    CHECK_FALSE(follow.update(10.0f, 0.9)); // looked back
    CHECK_FALSE(follow.update(90.0f, 1.0));
    CHECK_FALSE(follow.update(90.0f, 1.9));
    CHECK(follow.update(90.0f, 2.0));
}

TEST_CASE("reset forgets the wait, and unusable tuning falls back to the defaults") {
    PanelFollow follow;
    follow.update(90.0f, 0.0);
    follow.reset();
    CHECK_FALSE(follow.update(90.0f, 0.5));
    CHECK(follow.update(90.0f, 1.5));
    PanelFollow broken(PanelFollowTuning{-5.0f, std::nan("")});
    CHECK_FALSE(broken.update(59.0f, 0.0));
    CHECK_FALSE(broken.update(61.0f, 0.0));
    CHECK(broken.update(61.0f, 1.0));
}
