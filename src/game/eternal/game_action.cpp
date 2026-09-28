#include "game/eternal/game_action.hpp"

namespace evr::game {

std::string_view gameActionName(GameAction action) {
    switch (action) {
    case GameAction::Fire:
        return "fire";
    case GameAction::WeaponMod:
        return "weapon_mod";
    case GameAction::SwitchWeaponMod:
        return "switch_weapon_mod";
    case GameAction::Jump:
        return "jump";
    case GameAction::Dash:
        return "dash";
    case GameAction::Melee:
        return "melee";
    case GameAction::Chainsaw:
        return "chainsaw";
    case GameAction::FlameBelch:
        return "flame_belch";
    case GameAction::Equipment:
        return "equipment";
    case GameAction::SwitchEquipment:
        return "switch_equipment";
    case GameAction::QuickSwitch:
        return "quick_switch";
    case GameAction::WeaponWheel:
        return "weapon_wheel";
    case GameAction::NextWeapon:
        return "next_weapon";
    case GameAction::PreviousWeapon:
        return "previous_weapon";
    case GameAction::WeaponSlot1:
        return "weapon_slot_1";
    case GameAction::WeaponSlot2:
        return "weapon_slot_2";
    case GameAction::WeaponSlot3:
        return "weapon_slot_3";
    case GameAction::WeaponSlot4:
        return "weapon_slot_4";
    case GameAction::WeaponSlot5:
        return "weapon_slot_5";
    case GameAction::WeaponSlot6:
        return "weapon_slot_6";
    case GameAction::WeaponSlot7:
        return "weapon_slot_7";
    case GameAction::WeaponSlot8:
        return "weapon_slot_8";
    case GameAction::Crucible:
        return "crucible";
    case GameAction::Pause:
        return "pause";
    case GameAction::Dossier:
        return "dossier";
    case GameAction::MissionInfo:
        return "mission_info";
    case GameAction::Automap:
        return "automap";
    case GameAction::Recenter:
        return "recenter";
    case GameAction::Count:
        break;
    }
    return "unknown";
}

std::optional<GameAction> parseGameAction(std::string_view name) {
    for (std::size_t i = 0; i < kGameActionCount; ++i) {
        const auto action = static_cast<GameAction>(i);
        if (gameActionName(action) == name) {
            return action;
        }
    }
    return std::nullopt;
}

} // namespace evr::game
