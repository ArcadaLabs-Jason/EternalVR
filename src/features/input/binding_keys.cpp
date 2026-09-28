#include "features/input/binding_keys.hpp"

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace evr::input {

namespace {

constexpr std::string_view kWeaponHandKey = "weapon_hand";
constexpr std::string_view kStickToken = "stick";
constexpr std::string_view kRoleToken = "role";

constexpr std::array<std::pair<ButtonInput, std::string_view>, kButtonInputCount> kInputNames{{
    {ButtonInput::Trigger, "trigger"},
    {ButtonInput::Grip, "grip"},
    {ButtonInput::StickClick, "stick_click"},
    {ButtonInput::Primary, "primary"},
    {ButtonInput::Secondary, "secondary"},
    {ButtonInput::Menu, "menu"},
}};

constexpr std::array<std::pair<PressKind, std::string_view>, 3> kPressNames{{
    {PressKind::WhileDown, "press"},
    {PressKind::Tap, "tap"},
    {PressKind::Hold, "hold"},
}};

constexpr std::array<std::pair<StickGesture, std::string_view>, 3> kGestureNames{{
    {StickGesture::Up, "up"},
    {StickGesture::DownTap, "down_tap"},
    {StickGesture::DownHold, "down_hold"},
}};

constexpr std::array<std::pair<StickRole, std::string_view>, 3> kRoleNames{{
    {StickRole::None, "none"},
    {StickRole::Move, "move"},
    {StickRole::Turn, "turn"},
}};

template <typename Enum, std::size_t N>
std::string_view nameOf(const std::array<std::pair<Enum, std::string_view>, N>& table, Enum value) {
    for (const auto& [entry, name] : table) {
        if (entry == value) {
            return name;
        }
    }
    return {};
}

template <typename Enum, std::size_t N>
std::optional<Enum> valueOf(const std::array<std::pair<Enum, std::string_view>, N>& table,
                            std::string_view name) {
    for (const auto& [entry, entryName] : table) {
        if (entryName == name) {
            return entry;
        }
    }
    return std::nullopt;
}

std::vector<std::string_view> splitDots(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t dot = text.find('.', start);
        parts.push_back(text.substr(start, dot - start));
        if (dot == std::string_view::npos) {
            return parts;
        }
        start = dot + 1;
    }
}

std::optional<BindingKey> parseStickKey(Hand hand, std::string_view leaf) {
    if (leaf == kRoleToken) {
        return StickRoleKey{hand};
    }
    if (const auto gesture = valueOf(kGestureNames, leaf)) {
        return GestureKey{hand, *gesture};
    }
    return std::nullopt;
}

} // namespace

std::optional<BindingKey> parseBindingKey(std::string_view text) {
    if (text == kWeaponHandKey) {
        return WeaponHandKey{};
    }
    const std::vector<std::string_view> parts = splitDots(text);
    if (parts.size() != 3) {
        return std::nullopt;
    }
    const auto hand = parseHand(parts[0]);
    if (!hand) {
        return std::nullopt;
    }
    if (parts[1] == kStickToken) {
        return parseStickKey(*hand, parts[2]);
    }
    const auto input = valueOf(kInputNames, parts[1]);
    const auto press = valueOf(kPressNames, parts[2]);
    if (!input || !press) {
        return std::nullopt;
    }
    return ButtonKey{*hand, *input, *press};
}

std::string formatBindingKey(const BindingKey& key) {
    struct Formatter {
        std::string operator()(const WeaponHandKey& /*key*/) const { return std::string(kWeaponHandKey); }
        std::string operator()(const StickRoleKey& key) const {
            return std::string(handName(key.hand)) + ".stick.role";
        }
        std::string operator()(const ButtonKey& key) const {
            return std::string(handName(key.hand)) + "." + std::string(nameOf(kInputNames, key.input)) + "." +
                   std::string(nameOf(kPressNames, key.kind));
        }
        std::string operator()(const GestureKey& key) const {
            return std::string(handName(key.hand)) + ".stick." +
                   std::string(nameOf(kGestureNames, key.gesture));
        }
    };
    return std::visit(Formatter{}, key);
}

std::string_view handName(Hand hand) {
    return hand == Hand::Left ? "left" : "right";
}

std::optional<Hand> parseHand(std::string_view name) {
    if (name == "left") {
        return Hand::Left;
    }
    if (name == "right") {
        return Hand::Right;
    }
    return std::nullopt;
}

std::string_view stickRoleName(StickRole role) {
    return nameOf(kRoleNames, role);
}

std::optional<StickRole> parseStickRole(std::string_view name) {
    return valueOf(kRoleNames, name);
}

} // namespace evr::input
