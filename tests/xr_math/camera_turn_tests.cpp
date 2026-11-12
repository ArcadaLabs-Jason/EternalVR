// A pickup camera's turn on top of the head (camera_turn.hpp): the Praetor token's pickup as the rig traced
// it.

#include "xr_math/camera_turn.hpp"

#include "xr_math/camera_anim.hpp"
#include "xr_math/head_view.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <optional>

using evr::test::approxEqual;
using evr::xr_math::axisFromAngles;
using evr::xr_math::CameraTurn;
using evr::xr_math::CameraTurnTiming;
using evr::xr_math::IdAngles;

namespace {

constexpr double kFrame = 1.0 / 60.0;

std::optional<IdAngles> step(CameraTurn& turn, bool follow, IdAngles camera, double& seconds) {
    seconds += kFrame;
    return turn.update(follow, axisFromAngles(camera), seconds);
}

} // namespace

TEST_CASE("camera turn: nothing while the camera is not followed") {
    CameraTurn turn;
    double t = 0.0;
    CHECK_FALSE(step(turn, false, {15.0f, 45.0f, 0.0f}, t).has_value());
    CHECK_FALSE(step(turn, false, {2.0f, 90.0f, 3.0f}, t).has_value());
}

TEST_CASE("camera turn: the token's pickup plays its motion, leaves its cut out and eases back") {
    CameraTurn turn;
    double t = 0.0;
    // The first followed frame is the player's own view: the start of the turn.
    auto r = step(turn, true, {15.0f, 45.0f, 0.0f}, t);
    REQUIRE(r.has_value());
    CHECK(approxEqual(r->pitch, 0.0f));
    // The cut to the animation's start (9.4 degrees in one frame) is left out.
    r = step(turn, true, {5.59f, 45.0f, 0.01f}, t);
    REQUIRE(r.has_value());
    CHECK(approxEqual(r->pitch, 0.0f, 1e-2f));
    CHECK(turn.cuts() == 1);
    // Down to 11.4 over 1.3 s, a frame at a time: played.
    float pitch = 5.59f;
    while (pitch < 11.38f) {
        pitch = std::fmin(pitch + 0.08f, 11.38f);
        r = step(turn, true, {pitch, 45.0f, 0.01f}, t);
    }
    REQUIRE(r.has_value());
    CHECK(approxEqual(r->pitch, 11.38f - 5.59f, 1e-2f));
    CHECK(approxEqual(r->yaw, 0.0f, 1e-2f));
    // Up to 2.26 over 0.45 s: played, the turn ends 3.3 degrees up.
    while (pitch > 2.26f) {
        pitch = std::fmax(pitch - 0.6f, 2.26f);
        r = step(turn, true, {pitch, 45.0f, 0.01f}, t);
    }
    REQUIRE(r.has_value());
    CHECK(approxEqual(r->pitch, 2.26f - 5.59f, 1e-2f));
    CHECK(approxEqual(turn.largest().pitch, 11.38f - 5.59f, 1e-2f));
    // The game's camera back on the hand's aim in one frame: a cut, left out.
    r = step(turn, true, {15.0f, 45.0f, 0.0f}, t);
    CHECK(approxEqual(r->pitch, 2.26f - 5.59f, 1e-2f));
    CHECK(turn.cuts() == 2);
    // The pickup ends: the turn eases back to none over 0.3 s, never growing on the way.
    float last = std::fabs(r->pitch);
    int frames = 0;
    while ((r = step(turn, false, {15.0f, 45.0f, 0.0f}, t))) {
        CHECK(std::fabs(r->pitch) <= last + 1e-6f);
        last = std::fabs(r->pitch);
        ++frames;
    }
    CHECK(frames >= 16);
    CHECK(frames <= 19);
}

TEST_CASE("camera turn: yaw across its wrap, roll, and the limit") {
    CameraTurn turn(CameraTurnTiming{5.0f, 10.0f, 0.3});
    double t = 0.0;
    step(turn, true, {0.0f, 178.0f, 0.0f}, t);
    auto r = step(turn, true, {0.0f, -178.0f, 1.0f}, t); // 4 degrees of yaw across the wrap
    REQUIRE(r.has_value());
    CHECK(approxEqual(r->yaw, 4.0f, 1e-2f));
    CHECK(approxEqual(r->roll, 1.0f, 1e-2f));
    float yaw = -178.0f;
    for (int i = 0; i < 5; ++i) {
        yaw += 3.0f;
        r = step(turn, true, {0.0f, yaw, 1.0f}, t);
    }
    CHECK(approxEqual(r->yaw, 10.0f, 1e-2f)); // held at the limit
}

TEST_CASE("camera turn: followed again during a release, it goes on from what was left") {
    CameraTurn turn;
    double t = 0.0;
    step(turn, true, {0.0f, 0.0f, 0.0f}, t);
    for (int i = 1; i <= 10; ++i) {
        step(turn, true, {static_cast<float>(i), 0.0f, 0.0f}, t);
    }
    auto r = step(turn, false, {0.0f, 0.0f, 0.0f}, t);
    REQUIRE(r.has_value());
    r = step(turn, false, {0.0f, 0.0f, 0.0f}, t);
    REQUIRE(r.has_value());
    const float left = r->pitch;
    CHECK(left < 10.0f);
    CHECK(left > 0.0f);
    r = step(turn, true, {30.0f, 0.0f, 0.0f}, t); // a new camera: its first frame is its start
    REQUIRE(r.has_value());
    CHECK(approxEqual(r->pitch, left, 1e-4f));
    r = step(turn, true, {31.0f, 0.0f, 0.0f}, t);
    CHECK(approxEqual(r->pitch, left + 1.0f, 1e-2f));
}

TEST_CASE("camera turn: a camera or clock that is not finite") {
    CameraTurn turn;
    double t = 0.0;
    step(turn, true, {0.0f, 0.0f, 0.0f}, t);
    step(turn, true, {2.0f, 0.0f, 0.0f}, t);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    auto r = step(turn, true, {nan, 0.0f, 0.0f}, t);
    REQUIRE(r.has_value());
    CHECK(approxEqual(r->pitch, 2.0f, 1e-2f));
    r = turn.update(false, axisFromAngles({0.0f, 0.0f, 0.0f}), std::numeric_limits<double>::quiet_NaN());
    CHECK_FALSE(r.has_value());
    t += 1.0;
    CHECK_FALSE(step(turn, false, {0.0f, 0.0f, 0.0f}, t).has_value());
}

TEST_CASE("camera turn on the body: its yaw only on the player's own heading, never twice") {
    using evr::Quat;
    using evr::xr_math::anglesFromAxis;
    using evr::xr_math::composeHeadAxis;
    using evr::xr_math::headWithCameraAnim;
    using evr::xr_math::normalize180;
    using evr::xr_math::turnOnBody;
    using evr::xr_math::yawOnly;
    const Quat ahead{0.0f, 0.0f, 0.0f, 1.0f};
    // The player faces yaw 40; the pickup's camera has turned 8 down, 20 left and rolled 2 since it took the
    // view.
    const IdAngles turn{8.0f, 20.0f, 2.0f};
    const IdAngles camera{8.0f, 60.0f, 2.0f};
    // Head aim gave the body: the player's own heading. All of the turn goes on: the view is the camera's.
    const auto own = axisFromAngles({0.0f, 40.0f, 0.0f});
    const IdAngles a =
        anglesFromAxis(composeHeadAxis(own, headWithCameraAnim(own, ahead, turnOnBody(turn, true))));
    CHECK(approxEqual(a.pitch, camera.pitch, 1e-2f));
    CHECK(approxEqual(normalize180(a.yaw - camera.yaw), 0.0f, 1e-2f));
    CHECK(approxEqual(a.roll, camera.roll, 1e-2f));
    // Without head aim the body is the game's yaw, which already holds the camera's: the turn's yaw stays
    // out, so the view faces the camera's 60, not 80.
    const auto game = yawOnly(axisFromAngles(camera));
    REQUIRE(game.has_value());
    const IdAngles b =
        anglesFromAxis(composeHeadAxis(*game, headWithCameraAnim(*game, ahead, turnOnBody(turn, false))));
    CHECK(approxEqual(b.pitch, camera.pitch, 1e-2f));
    CHECK(approxEqual(normalize180(b.yaw - camera.yaw), 0.0f, 1e-2f));
    CHECK(approxEqual(b.roll, camera.roll, 1e-2f));
    const IdAngles twice = turnOnBody(turn, true);
    const IdAngles c = anglesFromAxis(composeHeadAxis(*game, headWithCameraAnim(*game, ahead, twice)));
    CHECK(approxEqual(normalize180(c.yaw - 80.0f), 0.0f, 1e-2f)); // what the fix keeps out
}
