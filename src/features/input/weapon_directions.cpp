#include "features/input/weapon_directions.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>

namespace evr::input {

namespace {

constexpr float kDegrees = std::numbers::pi_v<float> / 180.0f;

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

// Directions are counted in eighths counter-clockwise from the right (WheelDirection), any number of turns.
bool hasSlot(const WeaponDirections& table, int eighth) {
    const std::uint8_t slot = table[static_cast<std::size_t>(((eighth % 8) + 8) % 8)];
    return slot >= 1 && slot <= 8;
}

// Eighths from `eighth` to the next direction with a weapon, counter-clockwise (`step` 1) or clockwise (-1);
// 8 when there is none other.
int gapTo(const WeaponDirections& table, int eighth, int step) {
    for (int k = 1; k < 8; ++k) {
        if (hasSlot(table, eighth + step * k)) {
            return k;
        }
    }
    return 8;
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

WheelDirection slotDirection(Axis2 stick,
                             WheelDirection current,
                             const WeaponDirections& table,
                             float threshold,
                             float hysteresisDegrees) {
    const WheelDirection eighth = wheelDirection(stick, threshold);
    if (eighth == WheelDirection::None) {
        return WheelDirection::None;
    }
    const float angle = std::atan2(stick.y, stick.x);
    // The stick's angle from a direction's centre, -pi to pi (counter-clockwise positive).
    const auto off = [&](int direction) {
        const float centre = static_cast<float>(direction) * 45.0f * kDegrees;
        return std::remainder(angle - centre, 2.0f * std::numbers::pi_v<float>);
    };
    const int e = static_cast<int>(eighth);
    WheelDirection nearest = eighth;
    if (!hasSlot(table, e)) {
        // The nearer of the closest directions with a weapon on either side of the stick's eighth.
        const int ccw = gapTo(table, e, 1);
        if (ccw == 8) {
            return WheelDirection::None; // no direction has a weapon
        }
        const int cw = gapTo(table, e, -1);
        const float o = off(e);
        const bool counterClockwise =
            static_cast<float>(ccw) * 45.0f * kDegrees - o <= static_cast<float>(cw) * 45.0f * kDegrees + o;
        const int d = counterClockwise ? e + ccw : e - cw;
        nearest = static_cast<WheelDirection>(((d % 8) + 8) % 8);
    }
    if (current == WheelDirection::None || current == nearest || !hasSlot(table, static_cast<int>(current))) {
        return nearest;
    }
    // `current`'s part reaches halfway to the next direction with a weapon on the stick's side, then the
    // hysteresis (as the wheel's eighths: 22.5 degrees and the hysteresis with all eight set).
    const int c = static_cast<int>(current);
    const float o = off(c);
    const int gap = gapTo(table, c, o >= 0.0f ? 1 : -1);
    return std::fabs(o) <= (static_cast<float>(gap) * 22.5f + hysteresisDegrees) * kDegrees ? current
                                                                                            : nearest;
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
