#pragma once

// Selecting on the weapon wheel by pointing the weapon hand (ETERNALVR_WHEEL_SELECT=hand,
// docs/VR_CONTROLLERS.md "Weapon wheel").
//
// By default the stick points at the wheel (wheel_mouse.hpp). With `hand` the stick or button that holds
// the wheel only holds it open, and the weapon hand points: the direction the hand points now, compared
// with the direction it pointed when the wheel was taken, becomes the pointer the stick would have given.
//
// - The reference is the hand's pointing direction at the first frame the wheel is held with the hand
//   tracked, so it works whichever way the player faces and wherever the hand happens to be.
// - Turning the hand right gives a pointer to the right, up gives up (the stick's axes: x right, y up),
//   in a frame built from that direction and the room's up. The pointer's length is the angle between the
//   two directions over `fullDegrees`, capped at 1; its direction is the way the hand turned. So the
//   wheel's usual threshold (half deflection, WheelMouseSettings::selectThreshold) is 10 degrees at the
//   default 20: a smaller turn leaves the highlight where it is, as a stick near the centre does.
// - Only the pointing direction counts. Rolling the hand (twisting the wrist about the barrel) never moves
//   the pointer, and neither does a roll the hand had when the wheel was taken.
// - Rotation, not translation: a turn of the wrist is small, quick and precise, the same seated or
//   standing, and is how the menu laser already points. Moving the hand sideways would need a large arm
//   sweep, drifts with the body and the head, and has no natural centre.
// - A hand that loses tracking (or a pose that is not finite) gives no pointer, so the highlight stays;
//   the reference is kept until the wheel is let go.
//
// Pure: no Windows or OpenXR calls. Poses follow the tracking-space convention (+Y up, -Z forward).

#include "common/quat.hpp"
#include "features/input/axis2.hpp"

#include <cstdint>

namespace evr::input {

// What selects on the weapon wheel.
enum class WheelSelect : std::uint8_t {
    Stick, // the stick that holds the wheel points at it (default)
    Hand,  // the weapon hand points; the stick or button only holds the wheel
};

const char* wheelSelectName(WheelSelect select);

// The hand's turn that points all the way to the wheel's rim (degrees).
inline constexpr float kDefaultWheelHandDegrees = 20.0f;
inline constexpr float kMinWheelHandDegrees = 5.0f;
inline constexpr float kMaxWheelHandDegrees = 45.0f;

// The pointer for a hand whose aim orientation was `start` and is now `current`: x right and y up in the
// start's frame (the room's up), length the angle between the two pointing directions over
// `fullDegrees`, capped at 1. Zero for a non-finite or degenerate orientation, or `fullDegrees` outside
// its range (then the default is used).
Axis2 wheelHandPointer(Quat start, Quat current, float fullDegrees = kDefaultWheelHandDegrees);

class WheelHand {
public:
    explicit WheelHand(float fullDegrees = kDefaultWheelHandDegrees);

    // Once per command build. `held`: the wheel is held (the weapon_wheel action); `tracked` and `aim`: the
    // weapon hand's aim orientation. Returns the pointer for WheelMouse::update, zero while nothing points.
    Axis2 update(bool held, bool tracked, Quat aim);

    // Whether the reference direction was taken for this hold.
    [[nodiscard]] bool anchored() const { return anchored_; }
    [[nodiscard]] float fullDegrees() const { return fullDegrees_; }

private:
    float fullDegrees_;
    bool anchored_ = false;
    Quat start_;
};

} // namespace evr::input
