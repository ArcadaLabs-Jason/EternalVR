#include "xr_math/head_aim.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

using evr::test::approxEqual;
using evr::xr_math::axisFromAngles;
using evr::xr_math::drivenCamera;
using evr::xr_math::HeadAimState;
using evr::xr_math::headAimStep;
using evr::xr_math::IdViewAxis;
using evr::xr_math::menuBodyYaw;
using evr::xr_math::normalize180;
using evr::xr_math::noteWritten;

namespace {

// Head aim on: body yaw `body`, the head turned `head` degrees in yaw (injected into the game's yaw).
HeadAimState aimed(float body, float head) {
    HeadAimState state;
    headAimStep(state, {0.0f, body, 0.0f}, 0.0f, {0.0f, head, 0.0f}, false);
    noteWritten(state, head);
    return state;
}

// The view's yaw once the head (still where it was when the menu came up) is added to the body.
float viewYaw(float body, float head) {
    return normalize180(body + head);
}

} // namespace

TEST_CASE("a camera the game drives is told from the player's own view by 15 degrees") {
    const IdViewAxis player = axisFromAngles({0.0f, 30.0f, 0.0f});
    CHECK_FALSE(drivenCamera(player, player));
    CHECK_FALSE(drivenCamera(player, axisFromAngles({0.0f, 40.0f, 0.0f})));
    CHECK(drivenCamera(player, axisFromAngles({0.0f, 50.0f, 0.0f})));
    // The collectible viewer's shelf view looks 17 degrees further down than the player.
    CHECK(drivenCamera(axisFromAngles({17.2f, 0.0f, 0.0f}), axisFromAngles({0.0f, 0.0f, 0.0f})));
}

TEST_CASE("a menu over the player's own view keeps the body yaw held when it came up") {
    // The pause menu or a tutorial popup: the game renders the player's view angles (body 10 + head 20).
    const HeadAimState state = aimed(10.0f, 20.0f);
    const IdViewAxis player = axisFromAngles({5.0f, 30.0f, 0.0f});
    CHECK(approxEqual(menuBodyYaw(state, 10.0f, player, player), 10.0f));
    CHECK(approxEqual(menuBodyYaw(state, 10.0f, player, axisFromAngles({5.0f, 38.0f, 0.0f})), 10.0f));
}

TEST_CASE("the collectible viewer's close-up camera, 90 degrees from the player, is faced") {
    // Measured on the rig: the toys' close-up view rendered 90 degrees of yaw from the player's view angles
    // while the body was held at 0. Held, the view looked 90 degrees away from the toy, at an empty floor.
    const HeadAimState state = aimed(0.0f, 0.0f);
    const IdViewAxis player = axisFromAngles({17.2f, 0.0f, 0.0f});
    const IdViewAxis closeUp = axisFromAngles({0.0f, -90.0f, 0.0f});
    const float body = menuBodyYaw(state, 0.0f, player, closeUp);
    CHECK(approxEqual(body, -90.0f));
    CHECK(approxEqual(viewYaw(body, 0.0f), -90.0f));
}

TEST_CASE("under a driven camera the head turned when the menu came up is not added again") {
    // Body 10, head 20 left: the player saw yaw 30. The camera looks at yaw -60; with the head still 20 left
    // the view must face -60, so the body is -80.
    const HeadAimState state = aimed(10.0f, 20.0f);
    const float body = menuBodyYaw(state, 10.0f, axisFromAngles({0.0f, 30.0f, 0.0f}),
                                   axisFromAngles({-17.0f, -60.0f, 0.0f}));
    CHECK(approxEqual(body, -80.0f));
    CHECK(approxEqual(viewYaw(body, 20.0f), -60.0f));
}

TEST_CASE("the album viewer's camera across 180 degrees is faced") {
    // The album posters' camera, 90 degrees the other way (rig: +90 yaw with a little roll), from a player
    // facing 170.
    const HeadAimState state = aimed(165.0f, 5.0f);
    const float body = menuBodyYaw(state, 165.0f, axisFromAngles({0.0f, 170.0f, 0.0f}),
                                   axisFromAngles({10.0f, -100.0f, 3.6f}));
    CHECK(approxEqual(viewYaw(body, 5.0f), -100.0f, 1e-3f));
}

TEST_CASE("the shelf view, only looking further down than the player, keeps the held heading") {
    const HeadAimState state = aimed(-45.0f, 0.0f);
    const float body = menuBodyYaw(state, -45.0f, axisFromAngles({0.0f, -45.0f, 0.0f}),
                                   axisFromAngles({17.2f, -45.0f, 0.0f}));
    CHECK(approxEqual(body, -45.0f, 1e-3f));
}

TEST_CASE("a degenerate rendered axis leaves the held body yaw") {
    const HeadAimState state = aimed(10.0f, 20.0f);
    const IdViewAxis zero{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    CHECK(approxEqual(menuBodyYaw(state, 10.0f, axisFromAngles({0.0f, 30.0f, 0.0f}), zero), 10.0f));
}
