#include "game/eternal/quest_touch_bindings.hpp"

#include "features/input/binding_compiler.hpp"
#include "features/input/binding_keys.hpp"
#include "features/input/binding_text.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <array>
#include <initializer_list>
#include <ostream>
#include <variant>

using evr::game::Handedness;
using evr::game::questTouchBindingText;
using evr::input::BindingKey;
using evr::input::BindingMap;
using evr::input::BindingProfile;
using evr::input::buildBindingProfile;
using evr::input::ButtonInput;
using evr::input::ButtonKey;
using evr::input::formatBindingKey;
using evr::input::GestureKey;
using evr::input::Hand;
using evr::input::otherHand;
using evr::input::parseBindingKey;
using evr::input::parseBindingText;
using evr::input::StickRoleKey;
using evr::input::WeaponHandKey;
using evr::test::questTouchBindings;
using evr::test::questTouchProfile;

namespace {

constexpr std::array kAllHandedness{Handedness::Right, Handedness::LeftButtonSwap,
                                    Handedness::LeftButtonAndStickSwap};

// R06 section 4.2, applied to a right-handed key: which key the left-handed map binds instead.
BindingKey mirrored(const BindingKey& key, Handedness handedness) {
    const bool full = handedness == Handedness::LeftButtonAndStickSwap;
    if (const auto* button = std::get_if<ButtonKey>(&key)) {
        const bool swaps = full ? button->input != ButtonInput::Menu
                                : button->input == ButtonInput::Trigger ||
                                      button->input == ButtonInput::Grip ||
                                      button->input == ButtonInput::StickClick;
        return ButtonKey{swaps ? otherHand(button->hand) : button->hand, button->input, button->kind};
    }
    if (const auto* role = std::get_if<StickRoleKey>(&key)) {
        return StickRoleKey{full ? otherHand(role->hand) : role->hand};
    }
    if (const auto* gesture = std::get_if<GestureKey>(&key)) {
        return GestureKey{full ? otherHand(gesture->hand) : gesture->hand, gesture->gesture};
    }
    return key;
}

} // namespace

TEST_CASE("built-in maps parse and compile without issues") {
    for (const Handedness handedness : kAllHandedness) {
        const auto parsed = parseBindingText(questTouchBindingText(handedness));
        CHECK(parsed.issues.empty());
        const auto built = buildBindingProfile(parsed.entries);
        CHECK(built.ok());
        CHECK(built.profile.moveStick.has_value());
        CHECK(built.profile.turnStick.has_value());
        CHECK(built.profile.stickGestures.size() == 3);
    }
}

TEST_CASE("right-handed Quest Touch map follows R06") {
    const BindingMap map = questTouchBindings();
    const BindingMap expected{
        {"weapon_hand", "right"},
        {"left.stick.role", "move"},
        {"right.stick.role", "turn"},
        {"right.trigger.press", "fire"},
        {"right.grip.press", "weapon_mod"},
        {"right.primary.press", "jump"},
        {"right.secondary.press", "dash"},
        {"right.stick_click.press", "melee"},
        {"right.stick.up", "chainsaw"},
        {"right.stick.down_tap", "quick_switch"},
        {"right.stick.down_hold", "weapon_wheel"},
        {"left.trigger.press", "equipment"},
        {"left.grip.press", "flame_belch"},
        {"left.stick_click.press", "crucible"},
        {"left.primary.tap", "switch_equipment"},
        {"left.primary.hold", "dossier"},
        {"left.secondary.tap", "switch_weapon_mod"},
        {"left.secondary.hold", "mission_info"},
        {"left.menu.tap", "pause"},
    };
    CHECK(map == expected);
}

TEST_CASE("left-handed maps are the R06 mirrors of the right-handed map") {
    const BindingMap right = questTouchBindings(Handedness::Right);
    for (const Handedness handedness : {Handedness::LeftButtonSwap, Handedness::LeftButtonAndStickSwap}) {
        const BindingMap left = questTouchBindings(handedness);
        CHECK(left.size() == right.size());
        for (const auto& [key, value] : right) {
            const auto parsed = parseBindingKey(key);
            if (!parsed) {
                FAIL_CHECK("built-in key does not parse: " << key);
                continue;
            }
            if (std::holds_alternative<WeaponHandKey>(*parsed)) {
                CHECK(left.at(key) == "left");
                continue;
            }
            const std::string mirroredKey = formatBindingKey(mirrored(*parsed, handedness));
            CAPTURE(key);
            CAPTURE(mirroredKey);
            REQUIRE(left.contains(mirroredKey));
            CHECK(left.at(mirroredKey) == value);
        }
    }
}

TEST_CASE("the compiled built-in map matches its text") {
    for (const Handedness handedness : kAllHandedness) {
        const BindingProfile profile = questTouchProfile(handedness);
        CHECK(evr::input::toBindingMap(profile) == questTouchBindings(handedness));
    }
    CHECK(questTouchProfile().weaponHand == Hand::Right);
    CHECK(questTouchProfile(Handedness::LeftButtonSwap).weaponHand == Hand::Left);
}

TEST_CASE("built-in maps are written in the canonical binding format") {
    // Quoted keys under a [bindings] table, in the writer's order, so the built-in text reads the
    // same to a TOML parser as to ours.
    for (const Handedness handedness : kAllHandedness) {
        CHECK(evr::input::formatBindingText(questTouchBindings(handedness)) ==
              questTouchBindingText(handedness));
    }
}
