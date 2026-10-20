#include "features/input/gamepad_mode.hpp"

#include "features/input/forced_angles.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using evr::input::ForcedAngleGate;
using evr::input::ForcedAngleSignals;
using evr::input::ForcedReason;
using evr::input::gamepadModeHooks;
using evr::input::GamepadModeHooks;
using evr::input::kInhibitWheelBits;
using evr::input::wheelSignal;
using evr::input::yieldsAimOnly;

namespace {

// inhibitFlags values from players' logs ("forced view starts (...; inhibit 0x...)"): the weapon wheel, a
// pickup or glory kill's view, a climbable wall under the game's own takeover, and bits that came with forced
// view angles.
constexpr std::uint32_t kWheel = 0x18;
constexpr std::uint32_t kPickup = 0x1F87F;
constexpr std::uint32_t kPickupToken = 0x1F86F;
constexpr std::uint32_t kWallTakeover = 0xF;
constexpr std::uint32_t kOther[] = {0x7, 0x800, 0x10000, 0x1B800, 0x17040};

// One camera-hook frame of the gate as aim_hooks.cpp feeds it, in gamepad mode or with the controllers.
bool frame(ForcedAngleGate& gate, bool gamepadMode, std::uint32_t inhibit, bool wheelHeld = false) {
    ForcedAngleSignals s;
    s.inhibitFlags = inhibit;
    s.weaponWheel = wheelSignal(wheelHeld, gamepadMode, inhibit);
    return gate.update(s);
}

} // namespace

TEST_CASE("gamepad mode watches the pad and, on a known build, runs the view gate and the demon's head aim") {
    const GamepadModeHooks known = gamepadModeHooks(true, false);
    CHECK(known.padWatch);
    CHECK(known.viewGate);
    CHECK(known.demonAim);
    CHECK_FALSE(known.bhaptics);
    // An unknown build: the idPlayer offsets are not trusted, so only the pad (which needs none) is read.
    const GamepadModeHooks unknown = gamepadModeHooks(false, false);
    CHECK(unknown.padWatch);
    CHECK_FALSE(unknown.viewGate);
    CHECK_FALSE(unknown.demonAim);
}

TEST_CASE("gamepad mode starts bHaptics when it is on") {
    CHECK(gamepadModeHooks(true, true).bhaptics);
    CHECK_FALSE(gamepadModeHooks(true, false).bhaptics);
    // bHaptics checks the build itself and logs why it stays off, as with the controllers.
    CHECK(gamepadModeHooks(false, true).bhaptics);
}

TEST_CASE("in gamepad mode the weapon wheel's own bits are the wheel") {
    CHECK(wheelSignal(false, true, kWheel));
    CHECK(wheelSignal(false, true, kInhibitWheelBits));
    for (const std::uint32_t bits : {kPickup, kPickupToken, kWallTakeover, 0x8u, 0x10u, 0x0u}) {
        CHECK_FALSE(wheelSignal(false, true, bits));
    }
    for (const std::uint32_t bits : kOther) {
        CHECK_FALSE(wheelSignal(false, true, bits));
    }
}

TEST_CASE("with the controllers the wheel is still their wheel action held, as before") {
    CHECK_FALSE(wheelSignal(false, false, kWheel));
    CHECK(wheelSignal(true, false, kWheel));
    CHECK(wheelSignal(true, false, 0));
    CHECK(wheelSignal(true, true, kPickup));
}

TEST_CASE("gamepad mode: the pad's weapon wheel yields the aim only, a pickup's view yields everything") {
    ForcedAngleGate gate;
    CHECK(frame(gate, true, kWheel));
    CHECK(gate.reason() == ForcedReason::WeaponWheel);
    CHECK(yieldsAimOnly(gate.reason()));
    CHECK_FALSE(frame(gate, true, 0)); // no settling after the wheel
    CHECK(frame(gate, true, kPickup));
    CHECK(gate.reason() == ForcedReason::Inhibit);
    CHECK_FALSE(yieldsAimOnly(gate.reason()));
    // The same wheel bits with the controllers and their wheel button up stay a forced view, as before.
    ForcedAngleGate controllers;
    CHECK(frame(controllers, false, kWheel));
    CHECK(controllers.reason() == ForcedReason::Inhibit);
}
