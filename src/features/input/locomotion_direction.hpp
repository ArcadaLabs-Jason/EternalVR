#pragma once

// Which way "forward" on the move stick points (R06 section 2, R13 section 6.2).
//
// Movement is relative to the head by default, or to where the off hand points. Only the horizontal
// part of either direction counts, so looking or pointing up or down never tilts or slows movement,
// and nothing depends on head height: it works the same seated and standing.
//
// Under decoupled aim the game moves relative to its view yaw, which follows the weapon, not the
// head. The move vector is therefore rotated from the locomotion frame into the view frame before it
// reaches the game.

#include "common/pose.hpp"
#include "features/input/axis2.hpp"
#include "features/input/controller_state.hpp"

#include <cstdint>
#include <optional>

namespace evr::input {

enum class LocomotionFrame : std::uint8_t {
    Head,
    OffHand,
};

// Horizontal length the forward direction must keep before its yaw is trusted. 0.2 rejects anything
// within about 78 degrees of straight up or down: an off hand resting in a seated player's lap, or a
// head looking at the floor.
inline constexpr float kMinHorizontalForward = 0.2f;

// Yaw in radians of the pose's forward (-Z) direction projected onto the horizontal plane, measured
// counter-clockwise from -Z about +Y. Empty if the direction is too close to vertical.
std::optional<float> horizontalYaw(const Pose& pose, float minHorizontal = kMinHorizontalForward);

class LocomotionDirection {
public:
    // Returns the locomotion yaw in radians. The off-hand frame falls back to the head when the off
    // hand is untracked or points near vertically. If the head yaw is unusable too, the last good
    // yaw is kept rather than snapping movement to some arbitrary direction.
    float update(LocomotionFrame frame, const HeadState& head, const HandState& offHand);

private:
    float lastYaw_ = 0.0f;
};

// Rotates a move vector expressed relative to `locomotionYaw` so that it is relative to `viewYaw`
// (both radians, same convention as horizontalYaw).
Axis2 rotateIntoViewFrame(Axis2 move, float locomotionYaw, float viewYaw);

} // namespace evr::input
