#include "xr_math/head_aim.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

using evr::test::approxEqual;
using evr::xr_math::anglesFromAxis;
using evr::xr_math::axisFromAngles;
using evr::xr_math::cameraBodyYaw;
using evr::xr_math::cameraDrivesView;
using evr::xr_math::drivenBodyYaw;
using evr::xr_math::drivenCamera;
using evr::xr_math::HeadAimState;
using evr::xr_math::headAimStep;
using evr::xr_math::IdViewAxis;
using evr::xr_math::menuBodyYaw;
using evr::xr_math::normalize180;
using evr::xr_math::noteWritten;
using evr::xr_math::viewHeadYaw;

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

TEST_CASE("under a menu the pad's look stick turns the game's command, not the body the menu holds") {
    // Gamepad mode at Exultia's mod bot (rig, 2026-11-12, run pa2): the head still, the body -131.6 as the
    // menu came up, the game holding its view there while the pad's look stick turned its command; head aim's
    // own step took that turn into the body (53.1 as the menu closed, then a 175.3 degree turn in one frame
    // back to the game's -131.6). Held as under a menu, the body stays.
    HeadAimState state = aimed(-131.6f, 0.0f);
    const float delta = -131.6f; // the delta the game's view holds (the command 0)
    const evr::xr_math::HeadAimStep before =
        headAimStep(state, {0.0f, -131.6f, 0.0f}, delta, {0.0f, 0.0f, 0.0f}, false);
    CHECK(approxEqual(before.bodyYaw, -131.6f));
    noteWritten(state, delta + before.deltaYaw);
    const float turned = 184.7f; // the command's turn by the look stick under the menu
    const evr::xr_math::HeadAimStep drifted =
        headAimStep(state, {0.0f, normalize180(-131.6f + turned), 0.0f}, delta, {0.0f, 0.0f, 0.0f}, false);
    CHECK(approxEqual(drifted.bodyYaw, 53.1f, 1e-3f));
    const IdViewAxis held = axisFromAngles({0.0f, -131.6f, 0.0f}); // the game's view under the menu
    CHECK(approxEqual(menuBodyYaw(state, before.bodyYaw, held, held), -131.6f));
}

TEST_CASE("a pickup's camera shown with Still view drives every view of it, not only past 15 degrees") {
    // Head aim at the Sentinel Crystal (rig, 2026-11-12, run pa3, the head taken as still here): the player's
    // view angles stayed at the head's 129.88 through the sync while the camera swung out past 15 degrees
    // from them and back (its pitch and yaw in consecutive game views, traces 890 to 899 and 930 to 939).
    // Head aim's own body was 132.28.
    const IdViewAxis player = axisFromAngles({0.0f, 129.88f, 0.0f});
    constexpr std::array<std::array<float, 2>, 20> kCamera = {
        {{0.31f, 140.65f},  {0.75f, 142.20f},  {1.17f, 144.07f},  {1.60f, 146.05f},  {-0.04f, 147.63f},
         {-2.27f, 148.99f}, {-3.80f, 150.39f}, {-4.01f, 151.62f}, {-4.22f, 152.82f}, {-4.20f, 153.98f},
         {-2.68f, 145.86f}, {-3.08f, 144.81f}, {-3.51f, 143.71f}, {-3.91f, 142.65f}, {-4.42f, 141.87f},
         {-4.95f, 141.05f}, {-5.48f, 140.23f}, {-5.98f, 139.45f}, {-6.44f, 138.76f}, {-6.82f, 138.23f}}};
    const HeadAimState state = aimed(132.28f, 0.0f);
    // The body of one view: the camera's heading when it drives the view, else head aim's own.
    const auto body = [&](bool follow, const std::array<float, 2>& c) {
        const IdViewAxis camera = axisFromAngles({c[0], c[1], 0.0f});
        return cameraDrivesView(follow, player, camera)
                   ? drivenBodyYaw(state, anglesFromAxis(camera).yaw, 0.0f, false)
                   : 132.28f;
    };
    float alone = 0.0f;  // the largest step of the body by the 15 degrees alone
    float follow = 0.0f; // and with the pickup's camera driving every view
    float camera = 0.0f; // the camera's own largest step
    for (std::size_t i = 1; i < kCamera.size(); ++i) {
        if (i == 10) {
            continue; // the two runs of views are apart
        }
        alone =
            std::max(alone, std::fabs(normalize180(body(false, kCamera[i]) - body(false, kCamera[i - 1]))));
        follow =
            std::max(follow, std::fabs(normalize180(body(true, kCamera[i]) - body(true, kCamera[i - 1]))));
        camera = std::max(camera, std::fabs(kCamera[i][1] - kCamera[i - 1][1]));
    }
    // The 15 degrees alone: in to the camera's heading 13.8 degrees in one view (9.5 with the rig's head) and
    // back.
    CHECK(alone > 13.0f);
    // Followed in every view the body steps no more than the camera does.
    CHECK(follow <= camera + 1e-3f);
    CHECK(camera < 2.5f);
    // The camera within 15 degrees of the player's angles still drives the view of a pickup.
    CHECK_FALSE(drivenCamera(player, axisFromAngles({1.17f, 144.07f, 0.0f})));
    CHECK(cameraDrivesView(true, player, axisFromAngles({1.17f, 144.07f, 0.0f})));
    CHECK(cameraDrivesView(false, player, axisFromAngles({1.60f, 146.05f, 0.0f})));
}

TEST_CASE(
    "under a menu over a pickup shown with Still view the camera drives the view within 15 degrees too") {
    HeadAimState state = aimed(129.9f, 0.0f);
    const IdViewAxis player = axisFromAngles({0.0f, 129.9f, 0.0f});
    const IdViewAxis camera =
        axisFromAngles({0.0f, 135.0f, 0.0f}); // the crystal's own heading, 5.1 degrees off
    CHECK(approxEqual(menuBodyYaw(state, 129.9f, player, camera), 129.9f));
    CHECK(approxEqual(menuBodyYaw(state, 129.9f, player, camera, true), 135.0f, 1e-3f));
}

TEST_CASE(
    "a delta the game puts back as a pad's Use opens a menu is the head yaw a pickup's camera starts from") {
    // Gamepad mode at the Sentinel Crystal (rig, 2026-11-12, run pa9), the head sweeping: as Use opened the
    // upgrade menu the game put back a delta head aim had written with the head at -6.38 and kept it there,
    // while head aim went on injecting the head, last at 1.87. The player's view angles, and the crystal's
    // camera from them, held the -6.38: taken as 1.87, the view turned 8.2 degrees as the camera took over
    // and again as the camera cut to the crystal.
    constexpr float kBody = 178.51f;
    HeadAimState state = aimed(kBody, 0.0f);
    auto step = headAimStep(state, {0.0f, kBody, 0.0f}, 0.0f, {0.0f, -6.38f, 0.0f}, false);
    noteWritten(state, -6.38f);
    step = headAimStep(state, {0.0f, kBody - 6.38f, 0.0f}, -6.38f, {0.0f, 1.87f, 0.0f}, false);
    CHECK(approxEqual(step.bodyYaw, kBody, 1e-3f));
    noteWritten(state, 1.87f);
    CHECK(approxEqual(viewHeadYaw(state), 1.87f, 1e-3f)); // the write took
    // Use: the game puts the -6.38 delta back, frame after frame; head aim's own step keeps the body.
    for (int i = 0; i < 3; ++i) {
        step = headAimStep(state, {0.0f, kBody - 6.38f, 0.0f}, -6.38f, {0.0f, 1.87f, 0.0f}, true);
        CHECK(step.restoredWrite);
        CHECK(approxEqual(step.bodyYaw, kBody, 1e-3f));
        noteWritten(state, -6.38f + step.deltaYaw);
    }
    CHECK(approxEqual(viewHeadYaw(state), -6.38f, 1e-3f));
    // The menu is up and the crystal's camera starts at the player's view angles: the body stays.
    const IdViewAxis player = axisFromAngles({0.0f, kBody - 6.38f, 0.0f});
    CHECK(approxEqual(menuBodyYaw(state, kBody, player, player, true), kBody, 1e-3f));
    // Its cut to the crystal (135) turns the view once, by the camera's own turn, under the menu or after it.
    const float cut = menuBodyYaw(state, kBody, player, axisFromAngles({0.0f, 135.0f, 0.0f}), true);
    CHECK(approxEqual(cut, 141.38f, 1e-3f));
    CHECK(approxEqual(cameraBodyYaw(state, 135.0f), cut, 1e-3f));
    // The sync ends with the game setting the player's angles to its camera's (131.52): head aim takes that
    // value of the game's own with the head yaw the view held, so the body does not turn there either.
    step = headAimStep(state, {0.0f, 131.52f, 0.0f}, 131.52f, {0.0f, 3.0f, 0.0f}, true);
    CHECK_FALSE(step.restoredWrite);
    CHECK(approxEqual(step.bodyYaw, cameraBodyYaw(state, 131.52f), 1e-3f));
    CHECK(approxEqual(step.bodyYaw, 137.9f, 1e-3f));
    noteWritten(state, 131.52f + step.deltaYaw);
    // Once head aim's write holds again, the view holds the head injected last.
    step = headAimStep(state, {0.0f, 131.52f + step.deltaYaw, 0.0f}, 131.52f + step.deltaYaw,
                       {0.0f, 3.0f, 0.0f}, false);
    CHECK(approxEqual(step.bodyYaw, 137.9f, 1e-3f));
    CHECK(approxEqual(viewHeadYaw(state), 3.0f, 1e-3f));
}
