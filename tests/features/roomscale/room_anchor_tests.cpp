#include "features/roomscale/room_anchor.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>
#include <ostream>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::test::approxEqual;
using namespace evr::roomscale;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr Vec3 kUp{0.0f, 1.0f, 0.0f};
constexpr Vec3 kForward{0.0f, 0.0f, -1.0f};

Quat yawPitch(float yaw, float pitch) {
    return Quat::fromAxisAngle(kUp, yaw) * Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, pitch);
}

} // namespace

TEST_CASE("heading of level, pitched and vertical heads") {
    CHECK(headingOf(Quat::identity()) == doctest::Approx(0.0f));
    CHECK(headingOf(yawPitch(0.5f, 0.0f)) == doctest::Approx(0.5f));
    CHECK(headingOf(yawPitch(-2.0f, 0.3f)) == doctest::Approx(-2.0f));
    // Straight down and straight up keep the heading through the head's up direction.
    CHECK(headingOf(yawPitch(1.0f, -kPi / 2.0f)) == doctest::Approx(1.0f).epsilon(1e-3));
    CHECK(headingOf(yawPitch(1.0f, kPi / 2.0f)) == doctest::Approx(1.0f).epsilon(1e-3));
}

TEST_CASE("a full recenter puts the head at the room origin facing -Z") {
    const Pose head{yawPitch(0.8f, 0.2f), {0.4f, 1.15f, -0.7f}};
    const RoomAnchor anchor = recenter({}, head, RecenterKind::Full);
    CHECK(anchor.heightAnchored);
    const Pose room = toRoom(anchor, head);
    CHECK(approxEqual(room.position, Vec3{0.0f, 0.0f, 0.0f}));
    CHECK(headingOf(room.orientation) == doctest::Approx(0.0f));
    // The pitch survives: only yaw is taken out.
    const Vec3 forward = rotate(room.orientation, kForward);
    CHECK(forward.y == doctest::Approx(std::sin(0.2f)).epsilon(1e-4));
}

TEST_CASE("moves after a recenter are in the anchored head's frame") {
    // Anchored facing +X (yaw -90 degrees: -Z turned right).
    const Pose head{yawPitch(-kPi / 2.0f, 0.0f), {1.0f, 1.6f, 2.0f}};
    const RoomAnchor anchor = recenter({}, head, RecenterKind::Full);
    // Stepping 0.3 m along tracking +X is 0.3 m forward in the room (-Z), and 0.1 m down.
    const Pose moved{head.orientation, {1.3f, 1.5f, 2.0f}};
    CHECK(approxEqual(toRoom(anchor, moved).position, Vec3{0.0f, -0.1f, -0.3f}));
}

TEST_CASE("a yaw-and-origin recenter keeps the anchored height") {
    const RoomAnchor seated = recenter({}, Pose{Quat::identity(), {0.0f, 1.1f, 0.0f}}, RecenterKind::Full);
    const Pose stood{yawPitch(0.3f, 0.0f), {0.5f, 1.7f, 0.2f}};
    const RoomAnchor yawOnly = recenter(seated, stood, RecenterKind::YawAndOrigin);
    CHECK(yawOnly.origin.y == doctest::Approx(1.1f));
    CHECK(yawOnly.yaw == doctest::Approx(0.3f));
    CHECK(approxEqual(toRoom(yawOnly, stood).position, Vec3{0.0f, 0.6f, 0.0f}));
    // A full recenter after standing up corrects the height.
    const RoomAnchor full = recenter(yawOnly, stood, RecenterKind::Full);
    CHECK(approxEqual(toRoom(full, stood).position, Vec3{0.0f, 0.0f, 0.0f}));
}

TEST_CASE("a height re-anchor (the player stood up) keeps the heading and the horizontal origin") {
    // Seated at 0.25 m in LOCAL, facing -17.6 degrees (the owner's session 4).
    const Pose seatedHead{yawPitch(-0.307f, 0.0f), {0.345f, 0.254f, 0.755f}};
    const RoomAnchor seated = recenter({}, seatedHead, RecenterKind::Full);
    // Stood up 0.7 m, 0.3 m forward of the chair, looking elsewhere.
    const Pose stood{yawPitch(1.0f, 0.1f), {0.345f, 0.954f, 0.455f}};
    const RoomAnchor height = recenter(seated, stood, RecenterKind::Height);
    CHECK(height.heightAnchored);
    CHECK(height.yaw == doctest::Approx(seated.yaw));
    CHECK(height.origin.x == doctest::Approx(seated.origin.x));
    CHECK(height.origin.z == doctest::Approx(seated.origin.z));
    CHECK(height.origin.y == doctest::Approx(0.954f));
    // The head is back at the game's eye height; the forward step stays as a lean for body follow.
    const Vec3 room = toRoom(height, stood).position;
    CHECK(room.y == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(std::hypot(room.x, room.z) == doctest::Approx(0.3f));
}

TEST_CASE("the runtime's recenter after standing up re-anchors the height too") {
    // Seated anchor at 0.254 m in LOCAL; the player stands (head 0.95 m in LOCAL) and holds the headset's
    // own recenter. The runtime moves LOCAL so the head is at its origin (VDXR gives no pose: the move is
    // unknown and the anchor stays numerically where it was), then we re-anchor fully on the head.
    const RoomAnchor seated =
        recenter({}, Pose{yawPitch(-0.307f, 0.0f), {0.345f, 0.254f, 0.755f}}, RecenterKind::Full);
    const Pose headAfter{yawPitch(0.0f, 0.0f), {0.001f, 0.0f, 0.001f}};
    const RoomAnchor full = recenter(seated, headAfter, RecenterKind::Full);
    CHECK(approxEqual(toRoom(full, headAfter).position, Vec3{0.0f, 0.0f, 0.0f}));
    // With a known move the anchor first follows it; the result is the same full re-anchor.
    const Pose move{yawPitch(0.5f, 0.0f), {0.3f, 0.95f, -0.2f}};
    const RoomAnchor followed = afterSpaceChange(seated, move);
    const RoomAnchor refull = recenter(followed, headAfter, RecenterKind::Full);
    CHECK(approxEqual(toRoom(refull, headAfter).position, Vec3{0.0f, 0.0f, 0.0f}));
    CHECK(refull.yaw == doctest::Approx(0.0f).epsilon(1e-5));
}

TEST_CASE("a non-finite head leaves the anchor as it was") {
    const RoomAnchor anchor = recenter({}, Pose{Quat::identity(), {0.2f, 1.0f, 0.0f}}, RecenterKind::Full);
    const RoomAnchor same =
        recenter(anchor, Pose{Quat::identity(), {std::nanf(""), 1.0f, 0.0f}}, RecenterKind::Full);
    CHECK(same.origin.x == doctest::Approx(0.2f));
}

TEST_CASE("the room transform and its inverse agree") {
    const RoomAnchor anchor =
        recenter({}, Pose{yawPitch(1.2f, 0.0f), {0.3f, 1.4f, -0.2f}}, RecenterKind::Full);
    const Pose p{yawPitch(0.1f, -0.4f), {-0.5f, 1.0f, 0.9f}};
    const Pose back = compose(anchorPose(anchor), toRoom(anchor, p));
    CHECK(approxEqual(back.position, p.position));
    CHECK(approxEqual(rotate(back.orientation, kForward), rotate(p.orientation, kForward)));
}

TEST_CASE("a runtime space change leaves room poses of the same head unchanged") {
    const RoomAnchor anchor =
        recenter({}, Pose{yawPitch(0.7f, 0.0f), {0.2f, 1.2f, 0.1f}}, RecenterKind::Full);
    // The runtime recentred LOCAL on the head: the new origin at (0.5, 1.3, -0.4) in the old space, turned
    // 0.7 rad.
    const Pose newInPrevious{Quat::fromAxisAngle(kUp, 0.7f), {0.5f, 1.3f, -0.4f}};
    const RoomAnchor moved = afterSpaceChange(anchor, newInPrevious);
    const Pose headOld{yawPitch(0.9f, 0.1f), {0.6f, 1.25f, -0.5f}};
    const Pose headNew = compose(inverse(newInPrevious), headOld);
    const Pose roomOld = toRoom(anchor, headOld);
    const Pose roomNew = toRoom(moved, headNew);
    CHECK(approxEqual(roomOld.position, roomNew.position));
    CHECK(headingOf(roomOld.orientation) == doctest::Approx(headingOf(roomNew.orientation)));
    CHECK(moved.heightAnchored);
}
