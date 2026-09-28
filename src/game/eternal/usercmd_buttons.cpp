#include "game/eternal/usercmd_buttons.hpp"

#include <array>
#include <cstddef>

namespace evr::game {

namespace {

constexpr std::uint8_t kEscapeKey = 0x1B; // VK_ESCAPE

constexpr std::uint64_t weaponSlot(int slot) {
    return usercmd_button::kWeapon0 << slot;
}

// The keyboard keys of bindset 0 in the shipped DOOMEternalConfig.cfg
// (docs/notes/eternal-pc-keybinds.md), as Windows virtual-key codes.
struct DefaultKey {
    GameAction action;
    std::uint8_t key;
};
constexpr std::array kDefaultKeys{
    DefaultKey{GameAction::SwitchWeaponMod, 'F'}, // F _reload
    DefaultKey{GameAction::Jump, 0x20},           // SPACE _jump (VK_SPACE)
    DefaultKey{GameAction::Dash, 0xA0},           // LSHIFT _dash (VK_LSHIFT)
    DefaultKey{GameAction::Melee, 'E'},           // E _attack2 _use
    DefaultKey{GameAction::Chainsaw, 'C'},        // C _quick3
    DefaultKey{GameAction::FlameBelch, 'R'},      // R _bfg
    DefaultKey{GameAction::Equipment, 0xA2},      // LCTRL _quickuse (VK_LCONTROL)
    DefaultKey{GameAction::SwitchEquipment, 'G'}, // G _quick0
    DefaultKey{GameAction::QuickSwitch, 'Q'},     // Q _changeWeapon, tapped
    DefaultKey{GameAction::WeaponWheel, 'Q'},     // Q _changeWeapon, held
    DefaultKey{GameAction::WeaponSlot1, '1'},     // 1 _weap1
    DefaultKey{GameAction::WeaponSlot2, '2'},     // 2 _weap2
    DefaultKey{GameAction::WeaponSlot3, '3'},     // 3 _weap3
    DefaultKey{GameAction::WeaponSlot4, '4'},     // 4 _weap4
    DefaultKey{GameAction::WeaponSlot5, '5'},     // 5 _weap5
    DefaultKey{GameAction::WeaponSlot6, '6'},     // 6 _weap6
    DefaultKey{GameAction::WeaponSlot7, '7'},     // 7 _weap7
    DefaultKey{GameAction::WeaponSlot8, '8'},     // 8 _weap8
    DefaultKey{GameAction::Crucible, 'V'},        // V _crucible
    DefaultKey{GameAction::Pause, kEscapeKey},    // ESCAPE toggleMainMenu
    DefaultKey{GameAction::Dossier, 0x09},        // TAB _inventory (VK_TAB)
    DefaultKey{GameAction::MissionInfo, 0xA4},    // LALT _objectives (VK_LMENU)
};

} // namespace

std::uint64_t usercmdButtons(GameAction action) {
    using namespace usercmd_button;
    switch (action) {
    case GameAction::Fire:
        return kAttack1;
    case GameAction::WeaponMod:
        return kZoom | kAltFire;
    case GameAction::SwitchWeaponMod:
        return kReload;
    case GameAction::Jump:
        return kMoveUp;
    case GameAction::Dash:
        return kDash;
    case GameAction::Melee:
        return kAttack2 | kUse;
    case GameAction::Chainsaw:
        return kQuick3;
    case GameAction::FlameBelch:
        return kBfg;
    case GameAction::Equipment:
        return kQuickUse;
    case GameAction::SwitchEquipment:
        return kQuick0;
    case GameAction::QuickSwitch:
    case GameAction::WeaponWheel:
        return kChangeWeapon;
    case GameAction::NextWeapon:
        return kWeaponNext;
    case GameAction::PreviousWeapon:
        return kWeaponPrevious;
    case GameAction::WeaponSlot1:
        return weaponSlot(1);
    case GameAction::WeaponSlot2:
        return weaponSlot(2);
    case GameAction::WeaponSlot3:
        return weaponSlot(3);
    case GameAction::WeaponSlot4:
        return weaponSlot(4);
    case GameAction::WeaponSlot5:
        return weaponSlot(5);
    case GameAction::WeaponSlot6:
        return weaponSlot(6);
    case GameAction::WeaponSlot7:
        return weaponSlot(7);
    case GameAction::WeaponSlot8:
        return weaponSlot(8);
    case GameAction::Crucible:
        return kCrucible;
    case GameAction::Dossier:
    case GameAction::Automap: // the automap is a page of the Dossier
        return kInventory;
    case GameAction::MissionInfo:
        return kObjectives;
    case GameAction::Pause:
    case GameAction::Recenter:
    case GameAction::Count:
        break;
    }
    return 0;
}

std::uint64_t usercmdButtons(const GameActionSet& actions) {
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < kGameActionCount; ++i) {
        if (actions.test(i)) {
            bits |= usercmdButtons(static_cast<GameAction>(i));
        }
    }
    return bits != 0 ? (bits | usercmd_button::kAny) : 0;
}

int usercmdUpMove(const GameActionSet& actions) {
    return contains(actions, GameAction::Jump) ? 127 : 0;
}

std::optional<std::uint8_t> keyForAction(GameAction action) {
    if (action == GameAction::Pause) {
        return kEscapeKey;
    }
    return std::nullopt;
}

std::optional<std::uint8_t> defaultKey(GameAction action) {
    for (const DefaultKey& d : kDefaultKeys) {
        if (d.action == action) {
            return d.key;
        }
    }
    return std::nullopt;
}

std::string_view keyName(std::uint8_t virtualKey) {
    switch (virtualKey) {
    case 0x09:
        return "Tab";
    case 0x1B:
        return "Escape";
    case 0x20:
        return "Space";
    case 0xA0:
        return "Left Shift";
    case 0xA2:
        return "Left Ctrl";
    case 0xA4:
        return "Left Alt";
    default:
        break;
    }
    // Digits and letters are their own virtual-key codes.
    constexpr std::string_view kCharacters = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const std::size_t at = kCharacters.find(static_cast<char>(virtualKey));
    return at != std::string_view::npos ? kCharacters.substr(at, 1) : std::string_view{"?"};
}

} // namespace evr::game
