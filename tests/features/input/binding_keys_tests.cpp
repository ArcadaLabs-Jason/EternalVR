#include "features/input/binding_keys.hpp"

#include <doctest/doctest.h>

#include <initializer_list>
#include <ostream>
#include <string>

using evr::input::BindingKey;
using evr::input::ButtonInput;
using evr::input::ButtonKey;
using evr::input::formatBindingKey;
using evr::input::GestureKey;
using evr::input::Hand;
using evr::input::parseBindingKey;
using evr::input::parseStickRole;
using evr::input::PressKind;
using evr::input::StickGesture;
using evr::input::StickRole;
using evr::input::StickRoleKey;
using evr::input::WeaponHandKey;

TEST_CASE("keys parse into what they name") {
    CHECK(parseBindingKey("weapon_hand") == BindingKey{WeaponHandKey{}});
    CHECK(parseBindingKey("left.stick.role") == BindingKey{StickRoleKey{Hand::Left}});
    CHECK(parseBindingKey("right.trigger.press") ==
          BindingKey{ButtonKey{Hand::Right, ButtonInput::Trigger, PressKind::WhileDown}});
    CHECK(parseBindingKey("left.primary.hold") ==
          BindingKey{ButtonKey{Hand::Left, ButtonInput::Primary, PressKind::Hold}});
    CHECK(parseBindingKey("right.stick_click.tap") ==
          BindingKey{ButtonKey{Hand::Right, ButtonInput::StickClick, PressKind::Tap}});
    CHECK(parseBindingKey("right.stick.down_hold") ==
          BindingKey{GestureKey{Hand::Right, StickGesture::DownHold}});
}

TEST_CASE("unknown keys are rejected") {
    for (const char* key :
         {"", "weapon", "middle.trigger.press", "left.trigger", "left.trigger.squeeze", "left.thumb.press",
          "left.stick.sideways", "left.trigger.press.extra", "Left.trigger.press"}) {
        CAPTURE(key);
        CHECK_FALSE(parseBindingKey(key).has_value());
    }
}

TEST_CASE("every key formats back to the text it was parsed from") {
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        for (int input = 0; input < static_cast<int>(ButtonInput::Count); ++input) {
            for (const PressKind kind : {PressKind::WhileDown, PressKind::Tap, PressKind::Hold}) {
                const BindingKey key = ButtonKey{hand, static_cast<ButtonInput>(input), kind};
                CHECK(parseBindingKey(formatBindingKey(key)) == key);
            }
        }
        for (const StickGesture gesture : {StickGesture::Up, StickGesture::DownTap, StickGesture::DownHold}) {
            const BindingKey key = GestureKey{hand, gesture};
            CHECK(parseBindingKey(formatBindingKey(key)) == key);
        }
        const BindingKey role = StickRoleKey{hand};
        CHECK(parseBindingKey(formatBindingKey(role)) == role);
    }
    CHECK(formatBindingKey(WeaponHandKey{}) == "weapon_hand");
}

TEST_CASE("stick roles have names") {
    CHECK(parseStickRole("move") == StickRole::Move);
    CHECK(parseStickRole("turn") == StickRole::Turn);
    CHECK(parseStickRole("none") == StickRole::None);
    CHECK_FALSE(parseStickRole("look").has_value());
}
