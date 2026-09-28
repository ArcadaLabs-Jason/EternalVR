#pragma once

// The game's user command buttons and which of them each game action presses.
//
// The bits are the reflected `usercmdButton_t` enum of Steam build 25216728
// (docs/rig-findings/input-aim.md section 1.2). Which bits an action presses follows the Slayer key
// bindings the game ships (bindset 0 of DOOMEternalConfig.cfg): an action presses exactly what its
// default key presses, so the user command we write means the same to the game as the player pressing
// that key. Examples: the weapon-mod key is bound to `_zoom _altfire`, melee to `_attack2 _use`, Flame
// Belch to `_bfg`, the chainsaw to `_quick3` and switch equipment to `_quick0`.
//
// Writing the bits directly does not depend on the player's own key bindings; a rebound key changes
// nothing here. Pause has no bit: the game opens its menu from the Escape key's `toggleMainMenu`
// command, so the layer sends that key instead (keyForAction).

#include "game/eternal/game_action.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::game {

namespace usercmd_button {
inline constexpr std::uint64_t kAttack1 = 0x1;          // _attack1: fire
inline constexpr std::uint64_t kAttack2 = 0x2;          // _attack2: melee
inline constexpr std::uint64_t kAltFire = 0x4;          // _altfire: weapon mod
inline constexpr std::uint64_t kUse = 0x8;              // _use
inline constexpr std::uint64_t kZoom = 0x10;            // _zoom
inline constexpr std::uint64_t kChangeWeapon = 0x40;    // _changeWeapon: tap switches, hold opens the wheel
inline constexpr std::uint64_t kReload = 0x80;          // _reload: switch weapon mod
inline constexpr std::uint64_t kWeaponNext = 0x100;     // _weapnext
inline constexpr std::uint64_t kWeaponPrevious = 0x200; // _weapprev
inline constexpr std::uint64_t kWeapon0 = 0x400;        // _weap0; _weapN is kWeapon0 << N
inline constexpr std::uint64_t kBfg = 0x100000;         // _bfg: Flame Belch in the Slayer map
inline constexpr std::uint64_t kDash = 0x400000;        // _dash
inline constexpr std::uint64_t kQuickUse = 0x800000;    // _quickuse: the equipment launcher
inline constexpr std::uint64_t kQuick0 = 0x1000000;     // _quick0: switch equipment
inline constexpr std::uint64_t kQuick3 = 0x8000000;     // _quick3: the chainsaw
inline constexpr std::uint64_t kInventory = 0x40000000; // _inventory: the Dossier
inline constexpr std::uint64_t kMoveUp = 0x100000000;   // _jump
inline constexpr std::uint64_t kCrucible = 0x400000000; // _crucible
inline constexpr std::uint64_t kObjectives = 0x8000000000; // _objectives: mission information
// BUTTON_ANY (the enum's entry after INPUT_LOOKRIGHT): the keys set it with their own bit (seen on the rig
// for jump, dash, the weapon keys and most others), so a set of actions sends it too.
inline constexpr std::uint64_t kAny = 0x0200000000000000;
} // namespace usercmd_button

// The bits one action presses; 0 for an action with no bit (Pause).
std::uint64_t usercmdButtons(GameAction action);

// The bits a set of actions presses together, with BUTTON_ANY when any is pressed.
std::uint64_t usercmdButtons(const GameActionSet& actions);

// The command's up-move axis for a set of actions: the jump key sets upmove to 127 as well as its bit, and
// the player jumps on the axis (checked on the rig: the bit alone does nothing).
int usercmdUpMove(const GameActionSet& actions);

// The Windows virtual-key code an action is delivered as instead of a bit, if any (Pause: Escape).
std::optional<std::uint8_t> keyForAction(GameAction action);

// The Windows virtual-key code of the keyboard key the shipped Slayer binds (bindset 0) put an action on,
// if any: the key a tutorial popup names when it introduces the action ("[R]" for the Flame Belch). Fire,
// the weapon mod and next / previous weapon are on the mouse, the automap is a page of the Dossier and
// recenter is the layer's own, so they have none. A player's own key binds are not read.
std::optional<std::uint8_t> defaultKey(GameAction action);

// A key's name as the game's prompts show it ("R", "Left Shift"), for logs; "?" for a key defaultKey never
// gives.
std::string_view keyName(std::uint8_t virtualKey);

} // namespace evr::game
