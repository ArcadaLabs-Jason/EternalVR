#pragma once

// The room anchor: where the player's play space is centred and which way it faces (recenter, T-029,
// T-063).
//
// The runtime's LOCAL space is left as the runtime keeps it; the anchor is our own transform on top of
// it. Room space has its origin at the anchored head (horizontally, and vertically once the height has
// been anchored) and its -Z along the anchored head's heading, level with the floor. The game view is
// built from the head in room space, so a recenter turns and moves the game world around the player
// without touching what the compositor is given (the projection layer stays in LOCAL, with the poses
// the frames were really rendered from).
//
// All in OpenXR axes (+X right, +Y up, -Z forward), metres, yaw in radians about +Y (counter-clockwise
// seen from above; 0 faces -Z).

#include "common/pose.hpp"

namespace evr::roomscale {

struct RoomAnchor {
    float yaw = 0.0f; // heading of the room's -Z in the tracking space
    Vec3 origin{};    // the room's origin in the tracking space (y: the anchored head height)
    bool heightAnchored = false;
};

enum class RecenterKind {
    Full,         // yaw, horizontal origin and height: every recenter (ours and the runtime's) and the
                  // first stable pose
    YawAndOrigin, // yaw and horizontal origin; the height stays
    Height,       // the height only: the player stood up or sat down (posture re-detection)
};

// The heading of an orientation: its forward (-Z) projected on the floor. Looking straight up or down,
// the heading comes from the head's up direction instead. 0 for a degenerate orientation.
float headingOf(Quat orientation);

// The anchor recentred on `head` (tracking space).
RoomAnchor recenter(const RoomAnchor& current, const Pose& head, RecenterKind kind);

// The anchor as a pose in the tracking space (the room's origin and orientation), and its inverse, which
// takes tracking-space poses into room space.
Pose anchorPose(const RoomAnchor& anchor);
Pose roomFromTracking(const RoomAnchor& anchor);

// A tracking-space pose in room space.
Pose toRoom(const RoomAnchor& anchor, const Pose& tracking);

// The anchor carried along by the body (body follow, body_follow.hpp): `roomDelta` is how far the body
// moved, in room axes (its height is ignored), so a head that stays put in the tracking space ends up
// that much less far out in room space. The heading does not change.
RoomAnchor shiftedBy(const RoomAnchor& anchor, Vec3 roomDelta);

// The same physical anchor after the runtime moved its space: `newInPrevious` is the new space's origin
// in the previous space (XrEventDataReferenceSpaceChangePending::poseInPreviousSpace). Room-space poses of
// the same physical head are unchanged by the move. Tilt in `newInPrevious` is ignored (runtimes re-level).
RoomAnchor afterSpaceChange(const RoomAnchor& anchor, const Pose& newInPrevious);

} // namespace evr::roomscale
