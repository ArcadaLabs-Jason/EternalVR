#pragma once

// The Dossier map panned by its keys while the right mouse button rotates it (map_drag.hpp). The game's
// automap pans with W, A, S and D as well as with a left drag (idAutomap::HandleEvent compares their key
// numbers itself, as it does C's): pan += normalize(A - D, W - S) * automap_panSpeedKeyboard (1.25) * dt.
// A drag can only pan or rotate at a time (with both buttons held every motion pans), so while the other
// stick rotates, the pan stick holds the keys instead.
//
// The keys are on or off and the game normalises the pair, so a stick held part of the way, or between two
// directions, is spread over the frames: each frame holds the key pair (one of the eight directions, or
// none) that keeps the sum of what was held closest to the sum of the stick (error diffusion). Held at half
// deflection straight up, W is down every other frame. Pure.

#include "features/input/axis2.hpp"

#include <cstdint>

namespace evr::menu {

// The keys, as virtual-key codes (the layer sends their scan codes): the drag's directions, so W pans as a
// drag up does, D as a drag to the right.
inline constexpr std::uint8_t kMapKeyUp = 'W';
inline constexpr std::uint8_t kMapKeyLeft = 'A';
inline constexpr std::uint8_t kMapKeyDown = 'S';
inline constexpr std::uint8_t kMapKeyRight = 'D';

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
    // One frame: `pan` is the stick with its deadzone taken out (+x right, +y away from the player; at most
    // 1 long), the share of the keys' full speed and its direction. A stick at rest releases the keys and
    // forgets what was owed.
    MapKeys update(input::Axis2 pan);
    void reset();

private:
    input::Axis2 owed_; // the stick's sum less the keys' sum
};

} // namespace evr::menu
