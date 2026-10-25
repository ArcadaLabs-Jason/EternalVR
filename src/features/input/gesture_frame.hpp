#pragma once

// What the arm gestures, the reach to use and the wrist flick all read from a frame: the
// head's heading and whether a hand is tracked. Distances are measured from the eyes in the heading (the
// head's forward projected on the floor), so they work in any facing, seated and standing.

#include "common/vector.hpp"
#include "features/input/controller_state.hpp"

#include <cmath>
#include <optional>

namespace evr::input {

inline bool finiteVec(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// The head's heading: its forward on the floor, or with the head looking straight up or down, its up
// direction (which points along the heading looking down and against it looking up). Empty for a head
// that is not tracked.
inline std::optional<Vec3> headHeading(const HeadState& head) {
    if (!head.poseValid || !finiteVec(head.pose.position)) {
        return std::nullopt;
    }
    const Vec3 forward = transformDirection(head.pose, {0.0f, 0.0f, -1.0f});
    Vec3 flat{forward.x, 0.0f, forward.z};
    if (!(length(flat) >= 0.2f)) {
        const Vec3 up = transformDirection(head.pose, {0.0f, 1.0f, 0.0f});
        flat = Vec3{up.x, 0.0f, up.z} * (forward.y < 0.0f ? 1.0f : -1.0f);
    }
    if (!(length(flat) >= 1e-3f) || !finiteVec(flat)) {
        return std::nullopt;
    }
    return normalize(flat);
}

// A hand with a pose and a velocity, both finite.
inline bool handTracked(const HandState& hand) {
    return hand.poseValid && hand.velocityValid && finiteVec(hand.aimPose.position) &&
           finiteVec(hand.linearVelocity);
}

// A point in the head's frame: metres ahead along the heading, to the right of it, and above the eyes.
struct HeadFramePoint {
    float forward = 0.0f;
    float right = 0.0f;
    float up = 0.0f;
};

// `point` from the eyes at `eyes`, in the frame of the heading `forward` (a unit vector on the floor).
inline HeadFramePoint inHeadFrame(Vec3 point, Vec3 eyes, Vec3 forward) {
    const Vec3 offset = point - eyes;
    // forward x up is the right-hand side for -Z forward and +Y up.
    const Vec3 right = cross(forward, Vec3{0.0f, 1.0f, 0.0f});
    return {dot(offset, forward), dot(offset, right), offset.y};
}

} // namespace evr::input
