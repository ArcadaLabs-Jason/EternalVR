#include "features/input/gamepad_mode.hpp"

#include "features/input/forced_angles.hpp"

namespace evr::input {

GamepadModeHooks gamepadModeHooks(bool knownBuild, bool bhaptics) {
    GamepadModeHooks hooks;
    hooks.padWatch = true;
    hooks.viewGate = knownBuild;
    hooks.demonAim = knownBuild;
    hooks.bhaptics = bhaptics;
    return hooks;
}

bool wheelSignal(bool wheelHeld, bool gamepadMode, std::uint32_t inhibitFlags) {
    return wheelHeld || (gamepadMode && inhibitFlags == kInhibitWheelBits);
}

} // namespace evr::input
