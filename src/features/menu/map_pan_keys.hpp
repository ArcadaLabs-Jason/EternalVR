#pragma once

// The Dossier map panned by its keys while the right mouse button rotates it (map_drag.hpp). The game's
// automap pans with W, A, S and D as well as with a left drag (idAutomap::HandleEvent compares their key
// numbers itself, as it does C's): pan += normalize(A - D, W - S) * automap_panSpeedKeyboard (1.25) * dt.
// A drag can only pan or rotate at a time (with both buttons held every motion pans), so while the other
// stick rotates, the pan stick holds the keys instead.
//
// The keys are on or off and the game normalises the pair, so a stick held part of the way, or between two
// directions, is spread over time: the key pair (one of the eight directions, or none) that keeps what was
// held, over time, closest to what the stick asked for (error diffusion). The game reads the keys once per
// game frame, which is slower than the headset's, so a pair is held at least kKeyHoldSeconds: a key down and
// up again within one game frame would not count, and the pan would be uneven. Held at half deflection
// straight up, W is down for one hold and up for the next. Pure: time comes from the caller.

#include "features/input/axis2.hpp"

#include <cstdint>

namespace evr::menu {

// The keys, as virtual-key codes (the layer sends their scan codes): the drag's directions, so W pans as a
// drag up does, D as a drag to the right.
inline constexpr std::uint8_t kMapKeyUp = 'W';
inline constexpr std::uint8_t kMapKeyLeft = 'A';
inline constexpr std::uint8_t kMapKeyDown = 'S';
inline constexpr std::uint8_t kMapKeyRight = 'D';

// The shortest time a key pair is held (a game frame at 30 frames per second, with a margin).
inline constexpr double kKeyHoldSeconds = 0.034;

// Which of the four keys are held.
struct MapKeys {
    bool up = false;
    bool left = false;
    bool down = false;
    bool right = false;
    friend constexpr bool operator==(const MapKeys&, const MapKeys&) = default;
};

class MapPanKeys {
public:
    // One frame, `dt` seconds after the last: `pan` is the stick with its deadzone taken out (+x right, +y
    // away from the player; at most 1 long), the share of the keys' full speed and its direction. A stick at
    // rest releases the keys and forgets what was owed.
    MapKeys update(input::Axis2 pan, double dt);
    void reset();

private:
    input::Axis2 owed_;    // the stick's integral less the keys', in seconds at full speed
    input::Axis2 heldPan_; // what the game makes of the keys held now
    MapKeys held_;
    double heldFor_ = 0.0;
    bool holding_ = false; // a pair was chosen since the stick left rest
};

} // namespace evr::menu
