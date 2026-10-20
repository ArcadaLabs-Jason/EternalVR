#pragma once

// Gamepad mode: the motion controllers off (ETERNALVR_CONTROLLERS=0, the launcher's Motion controllers
// unticked), the game played with a pad, keyboard or mouse (docs/VR_CONTROLLERS.md "Gamepad mode"). The
// game's own input is never changed there: no pad button, stick or key is eaten or injected. What the layer
// adds only reads the game and the pad, except body follow's move (usercmd_hook_use.hpp) and head aim, as
// before:
// - the pad, read where the game reads it (pad_watch.hpp): both sticks held are the recenter hold, the sticks
//   move the comfort vignette;
// - the forced-view gate (forced_angles.hpp), from the read-only SetViewAngles hook and the camera hook: body
//   follow's and the vignette's forced-view input, the glory-kill options' sync kills, the weapon wheel's
//   quad;
// - bHaptics, which reads the game (the fire hook writes nothing with the controllers off);
// - head aim for a piloted demon (demon_view.hpp), as head aim has for the Slayer.
// The capture chord stays the controllers': on a pad every button and chord is the game's, and the chord
// would have to take its buttons away from the game.

#include <cstdint>

namespace evr::input {

struct GamepadModeHooks {
    bool padWatch = false; // the game's XInputGetState watched: the answer goes back unchanged
    bool viewGate = false; // SetViewAngles counted and the fire hook (both read only), the camera hook's gate
    bool demonAim = false; // head aim for a piloted demon
    bool bhaptics = false; // bHaptics' thread and its read-only hooks
};

// `knownBuild`: PlayerAim recognises the game build (the idPlayer offsets the gate, the shots and the piloted
// demon read); `bhaptics`: ETERNALVR_BHAPTICS (bHaptics checks the build itself, as with the controllers).
GamepadModeHooks gamepadModeHooks(bool knownBuild, bool bhaptics);

// The weapon wheel for the forced-view gate (ForcedAngleSignals::weaponWheel). With the controllers it is
// their wheel action held (`wheelHeld`). In gamepad mode the pad's wheel button is the game's own bind, so
// the wheel is told by the bits the game sets for it and nothing else (VIEW and BUTTONS, kInhibitWheelBits):
// in 56 session logs from players on builds that tell the wheel, each of 382 episodes of exactly those bits
// began with the wheel's button held, and none without it.
bool wheelSignal(bool wheelHeld, bool gamepadMode, std::uint32_t inhibitFlags);

} // namespace evr::input
