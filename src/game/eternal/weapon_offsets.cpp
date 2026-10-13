#include "game/eternal/weapon_offsets.hpp"

#include "common/parse_float.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace evr::game {

namespace {

constexpr float kMaxTranslation = 2.0f;
constexpr float kMaxAngle = 180.0f;
// A sight line further off the model's forward than this is a mistake in the table.
constexpr float kMaxSightAngle = 45.0f;

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

// Everything before a '#' that is not inside a quoted key.
std::string_view stripComment(std::string_view line) {
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') {
            quoted = !quoted;
        } else if (line[i] == '#' && !quoted) {
            return line.substr(0, i);
        }
    }
    return line;
}

std::optional<float> parseNumber(std::string_view text) {
    return parseFloat(trim(text));
}

// Comma-separated finite numbers, at most six of them; their count, or 0 for anything else.
std::size_t parseList(std::string_view text, std::array<float, 6>& v) {
    std::size_t count = 0;
    while (true) {
        const std::size_t comma = text.find(',');
        const std::string_view item = text.substr(0, comma);
        if (count == v.size()) {
            return 0;
        }
        const auto number = parseNumber(item);
        if (!number) {
            return 0;
        }
        v[count++] = *number;
        if (comma == std::string_view::npos) {
            return count;
        }
        text.remove_prefix(comma + 1);
    }
}

// An offset: exactly 3 or 6 numbers.
bool parseNumbers(std::string_view text, WeaponOffset& out) {
    std::array<float, 6> v{};
    const std::size_t count = parseList(text, v);
    if (count != 3 && count != 6) {
        return false;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (std::fabs(v[i]) > kMaxTranslation) {
            return false;
        }
    }
    for (std::size_t i = 3; i < 6; ++i) {
        if (std::fabs(v[i]) > kMaxAngle) {
            return false;
        }
    }
    out = {v[0], v[1], v[2], v[3], v[4], v[5]};
    return true;
}

// A sight line: exactly 2 angles.
bool parseSight(std::string_view text, WeaponSight& out) {
    std::array<float, 6> v{};
    if (parseList(text, v) != 2 || std::fabs(v[0]) > kMaxSightAngle || std::fabs(v[1]) > kMaxSightAngle) {
        return false;
    }
    out = {v[0], v[1]};
    return true;
}

// The entry for `name` itself or for the longest key it starts with followed by '_'; never "default".
template <typename Entry>
const Entry* specific(const std::map<std::string, Entry, std::less<>>& entries, std::string_view name) {
    if (const auto it = entries.find(name); it != entries.end()) {
        return &it->second;
    }
    const Entry* best = nullptr;
    std::size_t bestLength = 0;
    for (const auto& [key, entry] : entries) {
        if (key.size() < name.size() && name.starts_with(key) && name[key.size()] == '_' &&
            key.size() > bestLength) {
            best = &entry;
            bestLength = key.size();
        }
    }
    return best;
}

using OffsetMap = std::map<std::string, WeaponOffset, std::less<>>;

const WeaponOffset* fallback(const OffsetMap& entries) {
    const auto it = entries.find("default");
    return it == entries.end() ? nullptr : &it->second;
}

} // namespace

WeaponOffset WeaponOffsetTable::lookup(std::string_view declName, OffsetPosture posture) const {
    // Seated: the seated entry for the weapon, then the seated default, then the standing rules.
    if (posture == OffsetPosture::Seated) {
        if (const WeaponOffset* match = specific(seated, declName)) {
            return *match;
        }
        if (const WeaponOffset* match = fallback(seated)) {
            return *match;
        }
    }
    if (const WeaponOffset* match = specific(standing, declName)) {
        return *match;
    }
    const WeaponOffset* match = fallback(standing);
    return match ? *match : WeaponOffset{};
}

std::optional<WeaponSight> WeaponOffsetTable::lookupSight(std::string_view declName) const {
    const WeaponSight* match = specific(sights, declName);
    return match ? std::optional<WeaponSight>(*match) : std::nullopt;
}

WeaponOffsetTable parseWeaponOffsets(std::string_view text) {
    WeaponOffsetTable table;
    if (text.starts_with("\xEF\xBB\xBF")) {
        text.remove_prefix(3);
    }
    OffsetMap* section = &table.standing;
    bool inSights = false;
    int lineNumber = 0;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        const std::string_view raw = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
        ++lineNumber;
        const std::string_view line = trim(stripComment(raw));
        if (line.empty()) {
            continue;
        }
        const auto issue = [&](const char* what) {
            table.issues.push_back("line " + std::to_string(lineNumber) + ": " + what);
        };
        if (line.front() == '[') {
            inSights = false;
            if (line == "[standing]") {
                section = &table.standing;
            } else if (line == "[seated]") {
                section = &table.seated;
            } else if (line == "[sights]") {
                section = nullptr;
                inSights = true;
            } else {
                issue("unknown section (expected [standing], [seated] or [sights])");
                section = nullptr;
            }
            continue;
        }
        if (!section && !inSights) {
            continue; // entries of an unknown section were reported with its header
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            issue("expected \"name\" = [numbers]");
            continue;
        }
        std::string_view key = trim(line.substr(0, equals));
        std::string_view value = trim(line.substr(equals + 1));
        if (key.size() < 2 || key.front() != '"' || key.back() != '"') {
            issue("the name must be quoted");
            continue;
        }
        key = key.substr(1, key.size() - 2);
        if (key.empty()) {
            issue("empty name");
            continue;
        }
        if (inSights) {
            WeaponSight sight;
            if (key == "default") {
                issue("[sights] has no \"default\": a weapon without a row is not turned");
            } else if (value.size() < 2 || value.front() != '[' || value.back() != ']' ||
                       !parseSight(value.substr(1, value.size() - 2), sight)) {
                issue("expected [pitch, yaw]: two finite angles within 45 degrees");
            } else if (table.sights.contains(key)) {
                issue("duplicate name; the first one is kept");
            } else {
                table.sights.emplace(std::string(key), sight);
            }
            continue;
        }
        if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
            issue("the value must be a list: [forward, left, up, pitch, yaw, roll]");
            continue;
        }
        WeaponOffset offset;
        if (!parseNumbers(value.substr(1, value.size() - 2), offset)) {
            issue("expected 3 or 6 finite numbers (translations within 2 m, angles within 180 degrees)");
            continue;
        }
        if (section->contains(key)) {
            issue("duplicate name; the first one is kept");
            continue;
        }
        section->emplace(std::string(key), offset);
    }
    return table;
}

bool parseOffsetList(std::string_view text, WeaponOffset& out) {
    return parseNumbers(trim(text), out);
}

} // namespace evr::game
