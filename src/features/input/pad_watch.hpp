#pragma once

// Gamepad mode's read of the game's pad (gamepad_mode.hpp). Every XInputGetState call the game makes is seen
// after XInput answered and handed back to the game unchanged, so the layer reads what the game reads, for
// every pad it polls, and never adds a poll of its own. From the pads read:
// - both sticks pressed on one pad: the recenter hold (room_scale.hpp noteRecenterBinding counts its time),
// as
//   both sticks held on the motion controllers. The game sees both clicks too (the right one melees);
// - the move stick's deflection and the look stick's sideways deflection: the comfort vignette
// (vignette.hpp). A pad the game has not read for kPadStaleSeconds counts as let go. Pure: time comes from
// the caller.

#include "features/input/virtual_gamepad.hpp"

#include <array>
#include <optional>

namespace evr::input {

inline constexpr unsigned kPadUsers = 4;         // XUSER_MAX_COUNT
inline constexpr int kLeftStickDeadzone = 7849;  // XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE
inline constexpr int kRightStickDeadzone = 8689; // XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE
inline constexpr double kPadStaleSeconds = 0.25;

// A stick's deflection past `deadzone` (radial, in XInput units), 0 inside it to 1 at the rim.
float stickDeflection(int x, int y, int deadzone);

struct PadMotion {
    float move = 0.0f; // the left (move) stick's deflection, 0 to 1
    float turn = 0.0f; // the right (look) stick's sideways deflection, 0 to 1
};

class PadWatch {
public:
    // The game read pad `user` at `seconds`: its state, or nullopt when XInput answered that it is not
    // connected. Users from kPadUsers on are ignored.
    void update(unsigned user, const std::optional<PadState>& pad, double seconds);

    // Both sticks pressed on one pad the game read within kPadStaleSeconds of `seconds`.
    [[nodiscard]] bool bothSticks(double seconds) const;

    // The largest deflections among the pads the game read within kPadStaleSeconds of `seconds`.
    [[nodiscard]] PadMotion motion(double seconds) const;

private:
    struct Slot {
        bool connected = false;
        PadState pad;
        double seconds = -1.0;
    };
    [[nodiscard]] static bool fresh(const Slot& slot, double seconds);

    std::array<Slot, kPadUsers> slots_{};
};

} // namespace evr::input
