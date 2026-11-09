#pragma once

// The turn stick's directions (DECISIONS T-229): which part of the circle each direction gesture owns once a
// map binds the stick's sides.
//
// A map that binds none of left, right, up_left, up_right, down_left and down_right on the turn stick keeps
// the claim cones of turn_stick_arbiter.hpp: up and down within 30 degrees of straight up and down, turning
// everywhere else. One that binds any of them splits the stick by direction instead:
//
//   - each of the eight directions, 45 degrees apart, is owned by its gesture when the map binds one there
//     (up: `up`; down: `down`, `down_tap` or `down_hold`; the others by their names), and straight left and
//     right not bound are owned by turning;
//   - the stick belongs to the owned direction nearest its angle, so a direction that nobody owns is shared
//     by its nearest owned neighbours, halfway each (as weapon_directions.hpp shares an eighth with no
//     weapon): all eight owned, each has 45 degrees; up, down, left and right only, each has 90.
//
// A sweep is still claimed once and keeps its intent until the stick is back in the centre (the arbiter's
// rule against unwanted weapon swaps): a gesture's part claims at the gesture deflection, turning's at the
// turn deflection sideways. So a bound direction holds the stick's turning back for the whole of its own
// sweep and for no other; the directions turning owns turn as before. Bind left and right and the stick
// turns no more.

#include "features/input/axis2.hpp"
#include "features/input/binding_profile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace evr::input {

// Counter-clockwise from straight right, 45 degrees apart (as WheelDirection, wheel_mouse.hpp).
enum class StickDirection : std::uint8_t {
    Right,
    UpRight,
    Up,
    UpLeft,
    Left,
    DownLeft,
    Down,
    DownRight,
};

inline constexpr std::size_t kStickDirectionCount = 8;

enum class DirectionOwner : std::uint8_t {
    None,    // shared by its nearest owned neighbours
    Gesture, // a gesture of the map
    Turn,    // turning (straight left or right, not bound)
};

struct StickLayout {
    // A side of the stick (left, right or a diagonal) is bound: the stick is split by direction. Otherwise
    // the claim cones are used and `owners` is not read.
    bool directions = false;
    std::array<DirectionOwner, kStickDirectionCount> owners{};
    // `down` is bound: a down sweep is active for as long as it lasts, like up, with no tap, hold or stay
    // cone (the compiler drops down_tap and down_hold beside it).
    bool downPress = false;

    friend bool operator==(const StickLayout&, const StickLayout&) = default;
};

// The direction a gesture is pushed in: up for Up, down for Down, DownTap and DownHold.
StickDirection gestureDirection(StickGesture gesture);

// Whether the gesture is one of the stick's sides (left, right or a diagonal), whose binding splits the
// stick.
bool isSideGesture(StickGesture gesture);

// The layout of the turn stick's gestures (BindingProfile::stickGestures).
StickLayout stickLayoutOf(const std::vector<StickGestureBinding>& gestures);

// The owned direction whose part of the circle the stick is in: the nearest owned one by angle, an exact tie
// going counter-clockwise. Nullopt for a stick at the centre or not finite, and when no direction is owned.
std::optional<StickDirection> ownedDirectionAt(Axis2 stick, const StickLayout& layout);

// How wide `direction`'s part is, in degrees: halfway to the next owned direction on either side (360 when
// it is the only one); 0 when it is not owned.
float partDegrees(const StickLayout& layout, StickDirection direction);

// "up", "up_right", ... (the binding keys' names).
const char* stickDirectionName(StickDirection direction);

// For the log, clockwise from up: "up 90 deg, right 90 deg (turn), down 90 deg, left 90 deg (turn)". Empty
// when the stick is not split by direction.
std::string stickLayoutText(const StickLayout& layout);

} // namespace evr::input
