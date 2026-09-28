#pragma once

// Locomotion and turning in the form the game's user command takes them (M5, R13 section 6).
//
// Movement: the move stick, after its response curve, is a direction in the locomotion frame (the
// head, or the off hand; locomotion_direction.hpp). The game moves relative to its own view yaw, which
// under decoupled aim follows the weapon, so the vector is rotated into the view frame and then
// quantised to the command's integer move axes. Quantising keeps the direction: a diagonal at full
// deflection stays a unit-length diagonal, never the corner of the square.
//
// Turning: the turn policy gives float degrees per frame, while the command's angles are 16-bit
// fractions of a turn (id Tech's ANGLE2SHORT, 65536 per 360 degrees). The accumulator hands out whole
// units and carries the remainder, so any sequence of turns adds up to the exact sum requested: a
// 360-degree turn is 65536 units, and heading never drifts from rounding.

#include "common/vector.hpp"
#include "features/input/axis2.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/locomotion_direction.hpp"
#include "features/input/stick_response.hpp"

#include <cstdint>

namespace evr::input {

inline constexpr float kAngleUnitsPerDegree = 65536.0f / 360.0f;

// Integer move axes: +forward, +right, each within [-maxValue, maxValue].
struct MoveAxes {
    int forward = 0;
    int right = 0;

    friend constexpr bool operator==(MoveAxes, MoveAxes) = default;
};

// `move` (+y forward, +x right) scaled to `maxValue` with its length clamped to 1 first, rounded to the
// nearest integers. Non-finite input or a non-positive maxValue gives zero.
MoveAxes quantizeMove(Axis2 move, int maxValue);

// Hands out whole angle units for float degree deltas, carrying the remainder between frames.
class AngleUnitAccumulator {
public:
    // Adds `degrees` (positive turns left) and returns the whole units to add to the command's yaw this
    // frame. Non-finite input adds nothing.
    std::int32_t add(float degrees);

    // Degrees accumulated but not handed out yet, at most half a unit in magnitude.
    [[nodiscard]] float pendingDegrees() const;

    void reset() { pendingUnits_ = 0.0; }

private:
    double pendingUnits_ = 0.0;
};

// The move stick as a move in the game's view frame (+y forward along the view yaw, +x right).
class Locomotion {
public:
    explicit Locomotion(StickResponse response = kMoveStickResponse);

    // `stick` is the raw move-stick value; `offHand` the hand not holding the weapon; `viewYawRadians` the
    // game view's yaw in the tracking space (locomotion_direction.hpp convention). The result has length
    // at most 1.
    Axis2 update(Axis2 stick,
                 LocomotionFrame frame,
                 const HeadState& head,
                 const HandState& offHand,
                 float viewYawRadians);

    // The locomotion yaw the last update used (radians).
    [[nodiscard]] float lastYaw() const { return lastYaw_; }

private:
    StickResponse response_;
    LocomotionDirection direction_;
    float lastYaw_ = 0.0f;
};

// A move in the frame of `yawRadians` as a horizontal direction in tracking space (x right, z back,
// the OpenXR axes), for checks and for the vignette: where the player will actually go.
Vec3 moveInTrackingSpace(Axis2 move, float yawRadians);

} // namespace evr::input
