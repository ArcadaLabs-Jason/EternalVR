#pragma once

// The key grammar of binding text.
//
//   weapon_hand                    = "left" | "right"
//   <hand>.stick.role              = "move" | "turn" | "none"
//   <hand>.<input>.<press>         = <action>
//   <hand>.stick.<gesture>         = <action>      (only on the turn stick)
//
//   <hand>    left, right
//   <input>   trigger, grip, stick_click, primary (A/X), secondary (B/Y), menu
//   <press>   press (while down), tap, hold
//   <gesture> up, down_tap, down_hold
//   <action>  a game::gameActionName(), e.g. "fire", "weapon_wheel"
//
// Keys are written as quoted TOML keys ("left.trigger.press"), which TOML reads as one key; see
// binding_text.hpp for why the dotted form is not used.

#include "features/input/binding_profile.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace evr::input {

enum class StickRole : std::uint8_t {
    None,
    Move,
    Turn,
};

struct WeaponHandKey {
    friend bool operator==(const WeaponHandKey&, const WeaponHandKey&) = default;
};

struct StickRoleKey {
    Hand hand = Hand::Left;
    friend bool operator==(const StickRoleKey&, const StickRoleKey&) = default;
};

struct ButtonKey {
    Hand hand = Hand::Left;
    ButtonInput input = ButtonInput::Trigger;
    PressKind kind = PressKind::WhileDown;
    friend bool operator==(const ButtonKey&, const ButtonKey&) = default;
};

struct GestureKey {
    Hand hand = Hand::Right;
    StickGesture gesture = StickGesture::Up;
    friend bool operator==(const GestureKey&, const GestureKey&) = default;
};

using BindingKey = std::variant<WeaponHandKey, StickRoleKey, ButtonKey, GestureKey>;

std::optional<BindingKey> parseBindingKey(std::string_view text);
std::string formatBindingKey(const BindingKey& key);

std::string_view handName(Hand hand);
std::optional<Hand> parseHand(std::string_view name);

std::string_view stickRoleName(StickRole role);
std::optional<StickRole> parseStickRole(std::string_view name);

} // namespace evr::input
