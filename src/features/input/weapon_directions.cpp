#include "features/input/weapon_directions.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace evr::input {

namespace {

// Clockwise from up, as the setting is written.
constexpr std::array<std::pair<std::string_view, WheelDirection>, 8> kNames{{
    {"up", WheelDirection::Up},
    {"up_right", WheelDirection::UpRight},
    {"right", WheelDirection::Right},
    {"down_right", WheelDirection::DownRight},
    {"down", WheelDirection::Down},
    {"down_left", WheelDirection::DownLeft},
    {"left", WheelDirection::Left},
    {"up_left", WheelDirection::UpLeft},
}};

std::string_view trim(std::string_view s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

std::string normalized(std::string_view name) {
    std::string out;
    for (const char c : trim(name)) {
        out += c == '-' ? '_' : (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

std::optional<WheelDirection> parseDirection(std::string_view name) {
    const std::string key = normalized(name);
    for (const auto& [text, direction] : kNames) {
        if (key == text) {
            return direction;
        }
    }
    return std::nullopt;
}

std::optional<std::uint8_t> parseSlot(std::string_view text) {
    const std::string value = normalized(text);
    if (value == "none") {
        return 0;
    }
    if (value.size() == 1 && value[0] >= '0' && value[0] <= '8') {
        return static_cast<std::uint8_t>(value[0] - '0');
    }
    return std::nullopt;
}

} // namespace

WeaponDirectionsResult parseWeaponDirections(std::string_view text) {
    WeaponDirectionsResult result;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string_view entry = trim(text.substr(0, comma));
        text.remove_prefix(comma == std::string_view::npos ? text.size() : comma + 1);
        if (entry.empty()) {
            continue;
        }
        const std::size_t equals = entry.find('=');
        const auto direction =
            equals == std::string_view::npos ? std::nullopt : parseDirection(entry.substr(0, equals));
        const auto slot =
            equals == std::string_view::npos ? std::nullopt : parseSlot(entry.substr(equals + 1));
        if (!direction || !slot) {
            result.issues.push_back(
                "'" + std::string(entry) +
                "': expected <direction>=<slot 1 to 8, or 0 or none for nothing>, the direction one of up, "
                "up_right, right, down_right, down, down_left, left, up_left");
            continue;
        }
        result.table[static_cast<std::size_t>(*direction)] = *slot;
    }
    return result;
}

std::optional<game::GameAction> weaponSlotFor(const WeaponDirections& table, WheelDirection direction) {
    if (direction == WheelDirection::None) {
        return std::nullopt;
    }
    const std::uint8_t slot = table[static_cast<std::size_t>(direction)];
    if (slot < 1 || slot > 8) {
        return std::nullopt;
    }
    return static_cast<game::GameAction>(static_cast<int>(game::GameAction::WeaponSlot1) + slot - 1);
}

std::string weaponDirectionsText(const WeaponDirections& table) {
    std::string text;
    for (const auto& [name, direction] : kNames) {
        const std::uint8_t slot = table[static_cast<std::size_t>(direction)];
        text += text.empty() ? "" : ",";
        text += std::string(name) + "=" + (slot == 0 ? std::string("none") : std::to_string(slot));
    }
    return text;
}

} // namespace evr::input
