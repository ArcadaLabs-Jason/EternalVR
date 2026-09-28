#pragma once

// The game's actions: what a player can ask DOOM Eternal to do (docs/notes/eternal-pc-keybinds.md,
// R06 section 2). This is game-design data; the input mapper produces these, and the engine side
// turns them into the game's own input (R13 section 5), so remapping is entirely ours.

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::game {

enum class GameAction : std::uint8_t {
    Fire,
    WeaponMod, // Alt fire; the Meathook on the Super Shotgun.
    SwitchWeaponMod,
    Jump,
    Dash,
    Melee, // One input in the game: melee, Glory Kill, Blood Punch, and use/interact [U].
    Chainsaw,
    FlameBelch,
    Equipment,
    SwitchEquipment,
    QuickSwitch, // The weapon-switch key tapped: back to the last weapon.
    WeaponWheel, // The weapon-switch key held: the wheel, open while held.
    NextWeapon,
    PreviousWeapon,
    WeaponSlot1,
    WeaponSlot2,
    WeaponSlot3,
    WeaponSlot4,
    WeaponSlot5,
    WeaponSlot6,
    WeaponSlot7,
    WeaponSlot8,
    Crucible, // The Sentinel Hammer in The Ancient Gods Part Two.
    Pause,
    Dossier,
    MissionInfo,
    Automap,
    Recenter, // The layer's own: re-anchors the play space (long-press, docs/VR_ROOMSCALE.md).
    Count,
};

inline constexpr std::size_t kGameActionCount = static_cast<std::size_t>(GameAction::Count);

using GameActionSet = std::bitset<kGameActionCount>;

constexpr std::size_t actionIndex(GameAction action) {
    return static_cast<std::size_t>(action);
}

inline bool contains(const GameActionSet& set, GameAction action) {
    return set.test(actionIndex(action));
}

inline void add(GameActionSet& set, GameAction action) {
    set.set(actionIndex(action));
}

// True for the actions the layer handles itself and never sends to the game (Recenter).
constexpr bool isLayerAction(GameAction action) {
    return action == GameAction::Recenter;
}

// True for the actions that ask the game for a screen with its menu cursor (the pause menu, the Dossier and
// its pages). A cursor screen that comes up with none of these asked for is one the game raised on its own:
// a tutorial or lore popup.
constexpr bool opensMenu(GameAction action) {
    return action == GameAction::Pause || action == GameAction::Dossier ||
           action == GameAction::MissionInfo || action == GameAction::Automap;
}

// Stable lower_snake_case name, used in binding text and logs. Renaming one breaks saved bindings.
std::string_view gameActionName(GameAction action);

// The action with this name, if any.
std::optional<GameAction> parseGameAction(std::string_view name);

} // namespace evr::game
