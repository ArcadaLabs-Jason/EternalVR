#include "game/eternal/usercmd_buttons.hpp"

#include "features/input/binding_compiler.hpp"
#include "features/input/controller_bindings.hpp"
#include "game/eternal/controller_data.hpp"

#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

using evr::game::add;
using evr::game::builtinControllerData;
using evr::game::Controller;
using evr::game::GameAction;
using evr::game::gameActionName;
using evr::game::GameActionSet;
using evr::game::Handedness;
using evr::game::keyForAction;
using evr::game::kGameActionCount;
using evr::game::usercmdButtons;
namespace bits = evr::game::usercmd_button;

TEST_CASE("each action presses what its default Slayer key presses") {
    // bindset 0 of the shipped DOOMEternalConfig.cfg
    CHECK(usercmdButtons(GameAction::Fire) == 0x1);                  // MOUSE1 _attack1
    CHECK(usercmdButtons(GameAction::WeaponMod) == (0x10 | 0x4));    // MOUSE2 _zoom _altfire
    CHECK(usercmdButtons(GameAction::SwitchWeaponMod) == 0x80);      // F _reload
    CHECK(usercmdButtons(GameAction::Jump) == 0x100000000);          // SPACE _jump
    CHECK(usercmdButtons(GameAction::Dash) == 0x400000);             // LSHIFT _dash
    CHECK(usercmdButtons(GameAction::Melee) == (0x2 | 0x8));         // E _attack2 _use
    CHECK(usercmdButtons(GameAction::Chainsaw) == 0x8000000);        // C _quick3
    CHECK(usercmdButtons(GameAction::FlameBelch) == 0x100000);       // R _bfg
    CHECK(usercmdButtons(GameAction::Equipment) == 0x800000);        // LCTRL _quickuse
    CHECK(usercmdButtons(GameAction::SwitchEquipment) == 0x1000000); // G _quick0
    CHECK(usercmdButtons(GameAction::QuickSwitch) == 0x40);          // Q _changeWeapon (tap)
    CHECK(usercmdButtons(GameAction::WeaponWheel) == 0x40);          // Q _changeWeapon (hold)
    CHECK(usercmdButtons(GameAction::NextWeapon) == 0x100);          // MWHEELUP _weapnext
    CHECK(usercmdButtons(GameAction::PreviousWeapon) == 0x200);      // MWHEELDOWN _weapprev
    CHECK(usercmdButtons(GameAction::Crucible) == 0x400000000);      // V _crucible
    CHECK(usercmdButtons(GameAction::Dossier) == 0x40000000);        // TAB _inventory
    CHECK(usercmdButtons(GameAction::MissionInfo) == 0x8000000000);  // LALT _objectives
}

TEST_CASE("weapon slots 1 to 8 are _weap1 to _weap8") {
    constexpr std::array kSlots{GameAction::WeaponSlot1, GameAction::WeaponSlot2, GameAction::WeaponSlot3,
                                GameAction::WeaponSlot4, GameAction::WeaponSlot5, GameAction::WeaponSlot6,
                                GameAction::WeaponSlot7, GameAction::WeaponSlot8};
    for (std::size_t i = 0; i < kSlots.size(); ++i) {
        CAPTURE(i + 1);
        CHECK(usercmdButtons(kSlots[i]) == (bits::kWeapon0 << (i + 1)));
    }
}

TEST_CASE("pause is the Escape key, recenter is the layer's own; every other action has a bit") {
    CHECK(usercmdButtons(GameAction::Recenter) == 0);
    CHECK_FALSE(keyForAction(GameAction::Recenter).has_value());
    CHECK(usercmdButtons(GameAction::Pause) == 0);
    CHECK(keyForAction(GameAction::Pause) == std::uint8_t{0x1B});
    for (std::size_t i = 0; i < kGameActionCount; ++i) {
        const auto action = static_cast<GameAction>(i);
        CAPTURE(gameActionName(action));
        if (action != GameAction::Pause && action != GameAction::Recenter) {
            CHECK(usercmdButtons(action) != 0);
            CHECK_FALSE(keyForAction(action).has_value());
        }
    }
}

TEST_CASE("each action's default key is its key in the shipped Slayer binds") {
    using evr::game::defaultKey;
    // bindset 0 of the shipped DOOMEternalConfig.cfg, as Windows virtual-key codes
    CHECK(defaultKey(GameAction::SwitchWeaponMod) == std::uint8_t{'F'});     // F _reload
    CHECK(defaultKey(GameAction::Jump) == std::uint8_t{0x20});               // SPACE _jump
    CHECK(defaultKey(GameAction::Dash) == std::uint8_t{0xA0});               // LSHIFT _dash
    CHECK(defaultKey(GameAction::Melee) == std::uint8_t{'E'});               // E _attack2 _use
    CHECK(defaultKey(GameAction::Chainsaw) == std::uint8_t{'C'});            // C _quick3
    CHECK(defaultKey(GameAction::FlameBelch) == std::uint8_t{'R'});          // R _bfg
    CHECK(defaultKey(GameAction::Equipment) == std::uint8_t{0xA2});          // LCTRL _quickuse
    CHECK(defaultKey(GameAction::SwitchEquipment) == std::uint8_t{'G'});     // G _quick0
    CHECK(defaultKey(GameAction::QuickSwitch) == std::uint8_t{'Q'});         // Q _changeWeapon
    CHECK(defaultKey(GameAction::WeaponWheel) == std::uint8_t{'Q'});         // Q _changeWeapon
    CHECK(defaultKey(GameAction::Crucible) == std::uint8_t{'V'});            // V _crucible
    CHECK(defaultKey(GameAction::Pause) == keyForAction(GameAction::Pause)); // ESCAPE toggleMainMenu
    CHECK(defaultKey(GameAction::Dossier) == std::uint8_t{0x09});            // TAB _inventory
    CHECK(defaultKey(GameAction::MissionInfo) == std::uint8_t{0xA4});        // LALT _objectives
    constexpr std::array kSlots{GameAction::WeaponSlot1, GameAction::WeaponSlot2, GameAction::WeaponSlot3,
                                GameAction::WeaponSlot4, GameAction::WeaponSlot5, GameAction::WeaponSlot6,
                                GameAction::WeaponSlot7, GameAction::WeaponSlot8};
    for (std::size_t i = 0; i < kSlots.size(); ++i) {
        CAPTURE(i + 1);
        CHECK(defaultKey(kSlots[i]) == static_cast<std::uint8_t>('1' + i)); // 1 to 8 _weap1 to _weap8
    }
    // On the mouse, a page of the Dossier, or the layer's own: no key.
    for (const GameAction action : {GameAction::Fire, GameAction::WeaponMod, GameAction::NextWeapon,
                                    GameAction::PreviousWeapon, GameAction::Automap, GameAction::Recenter}) {
        CAPTURE(gameActionName(action));
        CHECK_FALSE(defaultKey(action).has_value());
    }
}

TEST_CASE("default keys have the names the game's prompts show") {
    using evr::game::keyName;
    CHECK(keyName('R') == "R");
    CHECK(keyName('F') == "F");
    CHECK(keyName('7') == "7");
    CHECK(keyName(0x20) == "Space");
    CHECK(keyName(0xA0) == "Left Shift");
    CHECK(keyName(0xA2) == "Left Ctrl");
    CHECK(keyName(0xA4) == "Left Alt");
    CHECK(keyName(0x09) == "Tab");
    CHECK(keyName(0x1B) == "Escape");
    CHECK(keyName(0x70) == "?"); // F1, never a default key
    for (std::size_t i = 0; i < kGameActionCount; ++i) {
        if (const auto key = evr::game::defaultKey(static_cast<GameAction>(i))) {
            CAPTURE(gameActionName(static_cast<GameAction>(i)));
            CHECK(keyName(*key) != "?");
        }
    }
}

TEST_CASE("jump also pushes the up-move axis, as the jump key does") {
    GameActionSet set;
    CHECK(evr::game::usercmdUpMove(set) == 0);
    add(set, GameAction::Dash);
    CHECK(evr::game::usercmdUpMove(set) == 0);
    add(set, GameAction::Jump);
    CHECK(evr::game::usercmdUpMove(set) == 127);
}

TEST_CASE("a set presses the union of its actions' bits") {
    GameActionSet set;
    CHECK(usercmdButtons(set) == 0);
    add(set, GameAction::Fire);
    add(set, GameAction::Jump);
    add(set, GameAction::Melee);
    CHECK(usercmdButtons(set) == (0x1 | 0x100000000 | 0x2 | 0x8 | bits::kAny));
    add(set, GameAction::Pause);
    CHECK(usercmdButtons(set) == (0x1 | 0x100000000 | 0x2 | 0x8 | bits::kAny));
    GameActionSet pauseOnly;
    add(pauseOnly, GameAction::Pause);
    CHECK(usercmdButtons(pauseOnly) == 0);
}

TEST_CASE("distinct gameplay actions press distinct bits, except the two faces of one key") {
    for (std::size_t a = 0; a < kGameActionCount; ++a) {
        for (std::size_t b = a + 1; b < kGameActionCount; ++b) {
            const auto first = static_cast<GameAction>(a);
            const auto second = static_cast<GameAction>(b);
            const bool sameKey = (first == GameAction::QuickSwitch && second == GameAction::WeaponWheel) ||
                                 (first == GameAction::Dossier && second == GameAction::Automap);
            CAPTURE(gameActionName(first));
            CAPTURE(gameActionName(second));
            if (!sameKey) {
                CHECK((usercmdButtons(first) & usercmdButtons(second)) == 0);
            }
        }
    }
}

TEST_CASE("every action in the default control maps reaches the game or the layer") {
    for (const Controller controller : evr::game::kControllers) {
        const auto data = evr::input::parseControllerData(builtinControllerData(controller));
        REQUIRE(data.ok());
        for (const auto& [handedness, map] : data.maps) {
            const auto built = evr::input::buildBindingProfile(map);
            REQUIRE(built.ok());
            for (const auto& binding : built.profile.buttons) {
                CAPTURE(gameActionName(binding.action));
                CHECK((usercmdButtons(binding.action) != 0 || keyForAction(binding.action).has_value() ||
                       evr::game::isLayerAction(binding.action)));
            }
            for (const auto& gesture : built.profile.stickGestures) {
                CAPTURE(gameActionName(gesture.action));
                CHECK(usercmdButtons(gesture.action) != 0);
            }
        }
    }
}

TEST_CASE("a usercmd button index names the action that presses it") {
    using evr::game::actionForUsercmdButton;
    // Indexes from the game's action table (build 25216728): _attack1 0, _attack2 1, _altfire 2, _use 3,
    // _zoom 4, _changeWeapon 6, _reload 7, _bfg 0x14, _dash 0x16, _quickuse 0x17, _quick0 0x18, _quick3 0x1B,
    // _inventory 0x1E, _jump 0x20, _crucible 0x22, _objectives 0x27.
    CHECK(actionForUsercmdButton(0) == GameAction::Fire);
    CHECK(actionForUsercmdButton(1) == GameAction::Melee);
    CHECK(actionForUsercmdButton(2) == GameAction::WeaponMod);
    CHECK(actionForUsercmdButton(3) == GameAction::Melee);
    CHECK(actionForUsercmdButton(4) == GameAction::WeaponMod);
    CHECK(actionForUsercmdButton(6) == GameAction::QuickSwitch);
    CHECK(actionForUsercmdButton(7) == GameAction::SwitchWeaponMod);
    CHECK(actionForUsercmdButton(0x14) == GameAction::FlameBelch);
    CHECK(actionForUsercmdButton(0x16) == GameAction::Dash);
    CHECK(actionForUsercmdButton(0x17) == GameAction::Equipment);
    CHECK(actionForUsercmdButton(0x18) == GameAction::SwitchEquipment);
    CHECK(actionForUsercmdButton(0x1B) == GameAction::Chainsaw);
    CHECK(actionForUsercmdButton(0x1E) == GameAction::Dossier);
    CHECK(actionForUsercmdButton(0x20) == GameAction::Jump);
    CHECK(actionForUsercmdButton(0x22) == GameAction::Crucible);
    CHECK(actionForUsercmdButton(0x27) == GameAction::MissionInfo);
    CHECK(actionForUsercmdButton(0x0B) == GameAction::WeaponSlot1);
    CHECK_FALSE(actionForUsercmdButton(0x1A)); // _quick2: no Slayer action; the Revenant's, through its binds
    CHECK_FALSE(actionForUsercmdButton(0x30)); // _crouch
    CHECK_FALSE(actionForUsercmdButton(57));   // BUTTON_ANY
    CHECK_FALSE(actionForUsercmdButton(-1));
    CHECK_FALSE(actionForUsercmdButton(64));
}
