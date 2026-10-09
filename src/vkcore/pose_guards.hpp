#pragma once

// Tracked positions checked before use (features/tracking/pose_guard.hpp, docs/VR_ROOMSCALE.md): the head the
// camera hook locates, and the hands and head the controllers locate at the XR worker's sync and at the
// camera hook. A position no head or hand could have reached from the last good one is held (the last good
// one is used) until it comes back, settles somewhere new or has been held too long. ETERNALVR_POSE_GUARD=0
// turns the guards off.

#include "common/vector.hpp"

#include <openxr/openxr.h>

#include <cstdint>

namespace evr::vkcore::pose_guards {

// One guard each; each is used by one thread only.
enum class Slot : std::uint8_t {
    Head,     // the camera hook's head (presenter_head.cpp)
    SyncHead, // the XR worker's sync (input_xr.cpp)
    SyncAimLeft,
    SyncAimRight,
    SyncGripLeft,
    SyncGripRight,
    ViewAimLeft, // the camera hook's hands (game_view_poses.cpp)
    ViewAimRight,
    ViewGripLeft,
    ViewGripRight,
    Count, // none
};

// Whether the guards are on (not ETERNALVR_POSE_GUARD=0); off, hand velocities are not limited either.
bool enabled();

// `position` (in LOCAL) located for `time`: replaced by the last good one when it jumped. True when it was.
bool check(Slot slot, Vec3& position, XrTime time);

// The runtime moved LOCAL or another reference space, or a new session began: every guard takes its next
// position as it is, and every position located for a time before `openUntil` (the change's time and a
// moment after it; 0 for none) as well. Any thread.
void resetAll(XrTime openUntil = 0);

} // namespace evr::vkcore::pose_guards
