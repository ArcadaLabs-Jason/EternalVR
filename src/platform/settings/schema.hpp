#pragma once

// The schema: every setting's key, type, default, limits and documentation in one place.
//
// All other settings code is data-driven from this, so the launcher can build its UI from the same
// definitions the layer validates against.

#include "platform/settings/setting_value.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::settings {

struct SettingDef {
    std::string key; // Dotted, lower_snake_case, e.g. "comfort.vignette_strength".
    SettingType type = SettingType::Bool;
    SettingValue defaultValue;
    // Inclusive range for Int and Float settings.
    std::optional<double> min;
    std::optional<double> max;
    // Allowed values for Enum settings.
    std::vector<std::string> choices;
    // True if the layer applies changes while the game is running (ARCHITECTURE section 12).
    bool liveTunable = false;
    std::string description;
};

enum class ValidationStatus : std::uint8_t {
    Ok,
    Clamped, // Out of range; the returned value is the nearest limit.
    UnknownKey,
    TypeMismatch,
    InvalidChoice, // Enum value not among the choices.
    NotFinite,     // NaN or infinity for a Float setting.
};

struct Validation {
    ValidationStatus status = ValidationStatus::Ok;
    // The value to store. Meaningful only when accepted().
    SettingValue value;
    // Empty for Ok; otherwise explains the problem for the log or the launcher.
    std::string message;

    [[nodiscard]] bool accepted() const {
        return status == ValidationStatus::Ok || status == ValidationStatus::Clamped;
    }
};

class Schema {
public:
    // Registers a definition. Returns false (and changes nothing) if the key already exists or the
    // default value itself fails validation, which would be a programming error in the schema.
    bool add(SettingDef def);

    [[nodiscard]] const SettingDef* find(std::string_view key) const;
    [[nodiscard]] std::vector<std::string> keys() const;

    // Checks a candidate value for `key`. TOML writes 1.0 and 1 differently and people editing by
    // hand will type either, so an Int value is accepted for a Float setting and converted, and a
    // Float value with no fractional part (45.0, not 45.5) is accepted for an Int setting.
    [[nodiscard]] Validation validate(std::string_view key, const SettingValue& value) const;

private:
    // std::less<> allows lookup by string_view without building a temporary string.
    std::map<std::string, SettingDef, std::less<>> defs_;
};

} // namespace evr::settings
