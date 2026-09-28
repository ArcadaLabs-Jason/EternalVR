#include "features/roomscale/room_anchor.hpp"

#include <cmath>

namespace evr::roomscale {

namespace {

constexpr Vec3 kUp{0.0f, 1.0f, 0.0f};

Quat yawRotation(float yaw) {
    return Quat::fromAxisAngle(kUp, yaw);
}

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

float headingOf(Quat orientation) {
    const Quat q = normalize(orientation);
    const Vec3 forward = rotate(q, Vec3{0.0f, 0.0f, -1.0f});
    Vec3 flat{forward.x, 0.0f, forward.z};
    if (length(flat) < 1e-3f) {
        // Straight down: the head's up points along the heading; straight up: against it.
        const Vec3 up = rotate(q, kUp);
        const float sign = forward.y < 0.0f ? 1.0f : -1.0f;
        flat = Vec3{up.x, 0.0f, up.z} * sign;
        if (length(flat) < 1e-3f) {
            return 0.0f;
        }
    }
    const float yaw = std::atan2(-flat.x, -flat.z);
    return std::isfinite(yaw) ? yaw : 0.0f;
}

RoomAnchor recenter(const RoomAnchor& current, const Pose& head, RecenterKind kind) {
    if (!finite(head.position)) {
        return current;
    }
    RoomAnchor next = current;
    if (kind != RecenterKind::Height) {
        next.yaw = headingOf(head.orientation);
        next.origin.x = head.position.x;
        next.origin.z = head.position.z;
    }
    if (kind != RecenterKind::YawAndOrigin) {
        next.origin.y = head.position.y;
        next.heightAnchored = true;
    }
    return next;
}

Pose anchorPose(const RoomAnchor& anchor) {
    return Pose{yawRotation(anchor.yaw), anchor.origin};
}

Pose roomFromTracking(const RoomAnchor& anchor) {
    return inverse(anchorPose(anchor));
}

Pose toRoom(const RoomAnchor& anchor, const Pose& tracking) {
    Pose room = compose(roomFromTracking(anchor), tracking);
    room.orientation = normalize(room.orientation);
    return room;
}

RoomAnchor shiftedBy(const RoomAnchor& anchor, Vec3 roomDelta) {
    const Vec3 flat{roomDelta.x, 0.0f, roomDelta.z};
    if (!finite(flat)) {
        return anchor;
    }
    RoomAnchor next = anchor;
    next.origin = anchor.origin + rotate(yawRotation(anchor.yaw), flat);
    return next;
}

RoomAnchor afterSpaceChange(const RoomAnchor& anchor, const Pose& newInPrevious) {
    if (!finite(newInPrevious.position)) {
        return anchor;
    }
    // Only the move's heading counts: runtimes keep their spaces level.
    const Pose level{yawRotation(headingOf(newInPrevious.orientation)), newInPrevious.position};
    // The anchor in the previous space is A; in the new one it is newInPrevious^-1 * A.
    const Pose moved = compose(inverse(level), anchorPose(anchor));
    RoomAnchor next = anchor;
    next.yaw = headingOf(moved.orientation);
    next.origin = moved.position;
    return next;
}

} // namespace evr::roomscale
