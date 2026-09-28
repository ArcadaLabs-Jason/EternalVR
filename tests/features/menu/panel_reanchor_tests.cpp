#include "features/menu/panel_pointer.hpp"
#include "features/roomscale/room_anchor.hpp"

#include <doctest/doctest.h>

#include <cmath>

// The menu panel lives in LOCAL; the controllers are located in room space (LOCAL under the recenter
// transform). After any re-anchor the ray must meet the panel where the hand really points (the owner's
// weapon upgrade screen while standing, 2026-09-27: the pointer and the game's cursor far apart).

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::menu::intersectPanel;
using evr::menu::localFromRoom;
using evr::menu::Panel;
using evr::roomscale::recenter;
using evr::roomscale::RecenterKind;
using evr::roomscale::RoomAnchor;
using evr::roomscale::roomFromTracking;
using evr::roomscale::toRoom;

namespace {

constexpr float kPi = 3.14159265358979f;

Quat yaw(float degrees) {
    return Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, degrees * kPi / 180.0f);
}

// A 2 m x 1.125 m panel 1.5 m in front of a standing head (LOCAL), facing it: placed as the menu places it.
Panel panelBefore(const Pose& head) {
    Panel p;
    p.pose.orientation = head.orientation;
    p.pose.position = head.position + evr::rotate(head.orientation, Vec3{0.0f, 0.0f, -1.5f});
    p.width = 2.0f;
    p.height = 1.125f;
    return p;
}

// A hand below and right of the head (LOCAL), aimed at `target` (LOCAL).
Pose handAimingAt(const Pose& head, Vec3 target) {
    const Vec3 hand = head.position + evr::rotate(head.orientation, Vec3{0.2f, -0.4f, -0.3f});
    const Vec3 d = normalize(target - hand);
    // -Z along d: yaw about +Y, then pitch about +X.
    const float yawRad = std::atan2(-d.x, -d.z);
    const float pitchRad = std::asin(d.y);
    Pose aim;
    aim.orientation =
        Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yawRad) * Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, pitchRad);
    aim.position = hand;
    return aim;
}

} // namespace

TEST_CASE("after a height re-anchor (standing up) the ray meets the panel where the hand points") {
    // Seated anchor, then the player stands and the posture re-detection re-anchors the height.
    const Pose seated{yaw(19.3f), {-0.003f, 0.54f, 0.47f}};
    const Pose standing{yaw(19.3f), {-0.003f, 1.117f, 0.47f}};
    RoomAnchor anchor = recenter({}, seated, RecenterKind::Full);
    anchor = recenter(anchor, standing, RecenterKind::Height);
    const Pose roomFromLocal = roomFromTracking(anchor);

    const Panel panel = panelBefore(standing);
    const Vec3 upperLeft = transformPoint(panel.pose, Vec3{-0.5f, 0.28125f, 0.0f}); // u 0.25, v 0.25
    const Pose localAim = handAimingAt(standing, upperLeft);
    const Pose roomAim = toRoom(anchor, localAim); // what the controller snapshot holds

    const auto hit = intersectPanel(panel, localFromRoom(roomFromLocal, roomAim));
    REQUIRE(hit);
    CHECK(hit->u == doctest::Approx(0.25f).epsilon(0.001));
    CHECK(hit->v == doctest::Approx(0.25f).epsilon(0.001));

    // The room-space pose itself is 1.1 m and 19 degrees away: taken as it was, it misses or lands far off.
    const auto wrong = intersectPanel(panel, roomAim);
    CHECK((!wrong || std::fabs(wrong->v - 0.25f) > 0.2f || std::fabs(wrong->u - 0.25f) > 0.2f));
}

TEST_CASE("after a full recenter facing the other way the ray still meets the panel at the right place") {
    const Pose first{yaw(19.3f), {0.0f, 1.1f, 0.47f}};
    const Pose turned{yaw(-170.2f), {0.684f, 1.117f, -0.472f}};
    RoomAnchor anchor = recenter({}, first, RecenterKind::Full);
    anchor = recenter(anchor, turned, RecenterKind::Full);
    const Panel panel = panelBefore(turned);
    const Vec3 centre = panel.pose.position;
    const Pose roomAim = toRoom(anchor, handAimingAt(turned, centre));
    const auto hit = intersectPanel(panel, localFromRoom(roomFromTracking(anchor), roomAim));
    REQUIRE(hit);
    CHECK(hit->u == doctest::Approx(0.5f).epsilon(0.001));
    CHECK(hit->v == doctest::Approx(0.5f).epsilon(0.001));
}

TEST_CASE("without a recenter the room transform is the identity and nothing changes") {
    const Pose aim{yaw(10.0f), {0.1f, 1.2f, -0.3f}};
    const Pose back = localFromRoom(Pose{}, aim);
    CHECK(back.position.x == doctest::Approx(aim.position.x));
    CHECK(back.position.y == doctest::Approx(aim.position.y));
    CHECK(back.position.z == doctest::Approx(aim.position.z));
    CHECK(std::fabs(back.orientation.y - aim.orientation.y) < 1e-6f);
}
