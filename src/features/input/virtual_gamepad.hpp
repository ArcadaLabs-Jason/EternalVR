#pragma once

// The virtual gamepad (T-009 L2, docs/rig-findings/input-aim.md section 5): our input as an XInput pad
// state, merged into what the game reads from XInputGetState when the user-command hooks are not in use.
//
// The game turns pad buttons into key events and runs them through its binds, so this path depends on
// the game's default pad binds (bindset 0 of the shipped config):
//   A jump, B dash, X chainsaw (_quick3), Y Flame Belch (_bfg), LB equipment (_quickuse),
//   RB weapon switch / wheel (_changeWeapon), right stick click melee (_attack2 _use), Start pause,
//   Back Dossier (_inventory), LT weapon mod (_zoom _altfire), RT fire, D-pad up switch weapon mod
//   (_reload), down mission info (_objectives), left switch equipment (_quick0), right Crucible.
// Actions without a pad bind (direct weapon slots, next and previous weapon) are not available here.

#include "features/input/axis2.hpp"
#include "game/eternal/game_action.hpp"

#include <cstdint>

namespace evr::input {

// XINPUT_GAMEPAD's layout and button bits.
namespace pad_button {
inline constexpr std::uint16_t kDpadUp = 0x0001;
inline constexpr std::uint16_t kDpadDown = 0x0002;
inline constexpr std::uint16_t kDpadLeft = 0x0004;
inline constexpr std::uint16_t kDpadRight = 0x0008;
inline constexpr std::uint16_t kStart = 0x0010;
inline constexpr std::uint16_t kBack = 0x0020;
inline constexpr std::uint16_t kLeftThumb = 0x0040;
inline constexpr std::uint16_t kRightThumb = 0x0080;
inline constexpr std::uint16_t kLeftShoulder = 0x0100;
inline constexpr std::uint16_t kRightShoulder = 0x0200;
inline constexpr std::uint16_t kA = 0x1000;
inline constexpr std::uint16_t kB = 0x2000;
inline constexpr std::uint16_t kX = 0x4000;
inline constexpr std::uint16_t kY = 0x8000;
} // namespace pad_button

struct PadState {
    std::uint16_t buttons = 0;
    std::uint8_t leftTrigger = 0;
    std::uint8_t rightTrigger = 0;
    std::int16_t thumbLX = 0;
    std::int16_t thumbLY = 0;
    std::int16_t thumbRX = 0;
    std::int16_t thumbRY = 0;

    friend bool operator==(const PadState&, const PadState&) = default;
};

// The pad state for held actions, a move (+y forward, +x right, length at most 1) and a look input on
// the right stick (the weapon-wheel pointer, or the turn stick when turning cannot go through the
// user command). Triggers are fully pressed or released.
PadState padStateFor(const game::GameActionSet& down, Axis2 move, Axis2 look);

// A real pad and ours read as one: buttons ORed, each trigger the larger, each stick axis the sum
// clamped to the axis range.
PadState mergePads(const PadState& real, const PadState& ours);

} // namespace evr::input
