#pragma once

// Typed setting values.
//
// The settings file is TOML, so values are limited to what TOML represents naturally. Enumerations
// are stored as their string names: the file stays readable and hand-editable, and adding a choice
// never renumbers existing ones.

#include <cstdint>
#include <string>
#include <variant>

namespace evr::settings {

enum class SettingType : std::uint8_t {
    Bool,
    Int,
    Float,
    String,
    Enum, // A string restricted to the schema's list of choices.
};

using SettingValue = std::variant<bool, std::int64_t, double, std::string>;

// The storage type a value of this setting type must hold. Enum shares String's storage.
bool holdsStorageFor(SettingType type, const SettingValue& value);

// For log and validation messages. Strings are quoted so empty values stay visible.
std::string toDisplayString(const SettingValue& value);

const char* toString(SettingType type);

} // namespace evr::settings
