// A view the game drives (a glory kill's forced angles or scripted camera) with the player turned round in
// the room: the view must face where the game points it, during the kill and after it, not that plus the
// head's yaw in the room (the standing glory-kill flip, session 5).

#include "xr_math/head_aim.hpp"
#include "xr_math/head_view.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <numbers>

using evr::Quat;
using evr::Vec3;
using evr::test::approxEqual;
using evr::xr_math::anglesFromAxis;
using evr::xr_math::axisFromAngles;
using evr::xr_math::composeHeadAxis;
using evr::xr_math::drivenBodyYaw;
using evr::xr_math::HeadAimState;
using evr::xr_math::headAimStep;
using evr::xr_math::headYawHeld;
using evr::xr_math::normalize180;
using evr::xr_math::noteWritten;

namespace {

// The rendered view's yaw: body yaw composed with a head turned `headYaw` (id Tech, about +Z).
float viewYaw(float bodyYaw, float headYaw) {
    const Quat head =
        Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, headYaw * std::numbers::pi_v<float> / 180.0f);
    return anglesFromAxis(composeHeadAxis(axisFromAngles({0.0f, bodyYaw, 0.0f}), head)).yaw;
}

bool sameYaw(float a, float b, float tolerance = 1e-2f) {
    return approxEqual(normalize180(a - b), 0.0f, tolerance);
}

// Head aim with the head turned `headYaw` in the room: body 0, so the game's yaw is the head's.
HeadAimState aimedAt(float headYaw) {
    HeadAimState state;
    headAimStep(state, {0.0f, 0.0f, 0.0f}, 0.0f, {0.0f, headYaw, 0.0f}, false);
    noteWritten(state, headYaw);
    const auto step = headAimStep(state, {0.0f, headYaw, 0.0f}, headYaw, {0.0f, headYaw, 0.0f}, false);
    REQUIRE(approxEqual(step.bodyYaw, 0.0f));
    REQUIRE(approxEqual(step.deltaYaw, 0.0f));
    noteWritten(state, headYaw);
    return state;
}

} // namespace

TEST_CASE("a glory kill while standing turned round in the room faces the demon, during and after") {
    // Standing, turned 170 degrees from the room's heading (session 5: head yaw 159 to 179 against the
    // anchor's heading of 19.3); the demon is 15 degrees to the right of where the player looks.
    constexpr float head = 170.0f;
    HeadAimState state = aimedAt(head);
    constexpr float demon = 185.0f;

    // The forced view: the game sets the delta so its yaw points at the demon.
    float body = drivenBodyYaw(state, demon, demon, true);
    CHECK(sameYaw(viewYaw(body, head), demon));
    // The head turns 10 degrees during the kill: the view turns with it from the demon.
    body = drivenBodyYaw(state, demon, demon, true);
    CHECK(sameYaw(viewYaw(body, head + 10.0f), demon + 10.0f));

    // The kill's scripted camera (heading 200, the delta untouched): the camera's heading, not 200 + 170.
    body = drivenBodyYaw(state, 200.0f, 0.0f, false);
    CHECK(sameYaw(viewYaw(body, head), 200.0f));

    // The kill ends with the game's yaw at the demon and the head back where it was: head aim resumes
    // without turning the view (the old code took the body to be 185 and faced 355, behind the player).
    auto step = headAimStep(state, {0.0f, demon, 0.0f}, demon, {0.0f, head, 0.0f}, true);
    CHECK_FALSE(step.restoredWrite);
    CHECK(sameYaw(step.bodyYaw, demon - head));
    CHECK(approxEqual(step.deltaYaw, 0.0f));
    CHECK(sameYaw(viewYaw(step.bodyYaw, head), demon));
    CHECK(sameYaw(step.bodyYaw + head + step.deltaYaw, demon)); // the game's yaw (the aim) stays there
    noteWritten(state, demon + step.deltaYaw);

    // Then the head turns 20 degrees left and the aim follows it from the demon.
    step = headAimStep(state, {0.0f, demon, 0.0f}, demon, {0.0f, head + 20.0f, 0.0f}, false);
    CHECK(sameYaw(step.bodyYaw, demon - head));
    CHECK(approxEqual(step.deltaYaw, 20.0f));
    CHECK(sameYaw(viewYaw(step.bodyYaw, head + 20.0f), demon + 20.0f));
}

TEST_CASE("a forced view that ends on a delta the game holds keeps the view still") {
    constexpr float head = -120.0f;
    HeadAimState state = aimedAt(head);
    // After the kill the game holds its delta at 45 for a few frames while the head moves.
    for (float h : {head, head + 5.0f, head + 12.0f}) {
        const auto step = headAimStep(state, {0.0f, 45.0f, 0.0f}, 45.0f, {0.0f, h, 0.0f}, true);
        CHECK(sameYaw(step.bodyYaw, 45.0f - head));
        CHECK(sameYaw(viewYaw(step.bodyYaw, h), 45.0f + (h - head)));
        noteWritten(state, 45.0f + step.deltaYaw);
    }
}

TEST_CASE("seated, facing the room's heading, a forced view is where the game points it") {
    HeadAimState state = aimedAt(0.0f);
    CHECK(sameYaw(viewYaw(drivenBodyYaw(state, 75.0f, 75.0f, true), 0.0f), 75.0f));
    const auto step = headAimStep(state, {0.0f, 75.0f, 0.0f}, 75.0f, {0.0f, 0.0f, 0.0f}, true);
    CHECK(sameYaw(step.bodyYaw, 75.0f));
    CHECK(approxEqual(step.deltaYaw, 0.0f));
}

TEST_CASE("a forced view keeps a delta head aim wrote at the head yaw it carried") {
    HeadAimState state = aimedAt(30.0f);
    // A value written earlier (head 30) comes back while the head is elsewhere: it holds 30, not the last.
    headAimStep(state, {0.0f, 30.0f, 0.0f}, 30.0f, {0.0f, 50.0f, 0.0f}, false);
    noteWritten(state, 50.0f);
    CHECK(approxEqual(headYawHeld(state, 30.0f, true), 30.0f));
    CHECK(approxEqual(headYawHeld(state, 50.0f, false), 50.0f));
    CHECK(approxEqual(headYawHeld(state, 99.0f, true), 50.0f)); // the game's own value: the last injected
}

TEST_CASE("before head aim has written, a driven view is the game's own yaw") {
    const HeadAimState state;
    CHECK(approxEqual(headYawHeld(state, 10.0f, true), 0.0f));
    CHECK(approxEqual(drivenBodyYaw(state, 123.0f, 0.0f, false), 123.0f));
}
