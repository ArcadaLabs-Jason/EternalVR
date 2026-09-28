#include "platform/settings/schema.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace evr::settings {

namespace {

Validation reject(ValidationStatus status, std::string message) {
    return {status, SettingValue{}, std::move(message)};
}

// Clamps a numeric value into [min, max]. `Number` is std::int64_t or double. Comparisons happen
// in double, but the limits are only converted back to `Number` when they apply, so large integers
// never round-trip through double.
template <typename Number>
Validation clampToRange(const SettingDef& def, Number value) {
    Number result = value;
    if (def.min && static_cast<double>(value) < *def.min) {
        result = static_cast<Number>(*def.min);
    }
    if (def.max && static_cast<double>(value) > *def.max) {
        result = static_cast<Number>(*def.max);
    }
    if (result == value) {
        return {ValidationStatus::Ok, SettingValue{value}, {}};
    }
    return {ValidationStatus::Clamped, SettingValue{result},
            def.key + ": " + toDisplayString(SettingValue{value}) + " is out of range, clamped to " +
                toDisplayString(SettingValue{result})};
}

Validation validateFloat(const SettingDef& def, double value) {
    // NaN would pass every range comparison unchanged, so reject it (and infinities) explicitly.
    if (!std::isfinite(value)) {
        return reject(ValidationStatus::NotFinite, def.key + ": value is not a finite number");
    }
    return clampToRange(def, value);
}

// TOML distinguishes 45 from 45.0, and a hand-edited file will contain either. A float with no
// fractional part is taken as the integer it spells; anything else is a type mismatch rather than
// a silent rounding.
Validation validateIntFromFloat(const SettingDef& def, double value) {
    // 2^63 is exactly representable; every double below it (and at or above -2^63) converts safely.
    constexpr double kInt64Limit = 9223372036854775808.0;
    const bool integral = std::isfinite(value) && std::trunc(value) == value;
    if (!integral || value < -kInt64Limit || value >= kInt64Limit) {
        return reject(ValidationStatus::TypeMismatch,
                      def.key + ": expected a whole number, got " + toDisplayString(SettingValue{value}));
    }
    return clampToRange(def, static_cast<std::int64_t>(value));
}

Validation validateEnum(const SettingDef& def, const std::string& value) {
    const bool known = std::find(def.choices.begin(), def.choices.end(), value) != def.choices.end();
    if (!known) {
        return reject(ValidationStatus::InvalidChoice,
                      def.key + ": \"" + value + "\" is not one of the allowed choices");
    }
    return {ValidationStatus::Ok, SettingValue{value}, {}};
}

} // namespace

bool Schema::add(SettingDef def) {
    if (defs_.contains(def.key)) {
        return false;
    }
    const std::string key = def.key;
    const SettingValue defaultValue = def.defaultValue;
    defs_.emplace(key, std::move(def));

    // A default that does not validate cleanly is a bug in the schema itself; refuse it.
    if (validate(key, defaultValue).status != ValidationStatus::Ok) {
        defs_.erase(key);
        return false;
    }
    return true;
}

const SettingDef* Schema::find(std::string_view key) const {
    const auto it = defs_.find(key);
    return (it != defs_.end()) ? &it->second : nullptr;
}

std::vector<std::string> Schema::keys() const {
    std::vector<std::string> result;
    result.reserve(defs_.size());
    for (const auto& [key, def] : defs_) {
        result.push_back(key);
    }
    return result;
}

Validation Schema::validate(std::string_view key, const SettingValue& value) const {
    const SettingDef* def = find(key);
    if (def == nullptr) {
        return reject(ValidationStatus::UnknownKey, "unknown setting '" + std::string(key) + "'");
    }

    if (def->type == SettingType::Float && std::holds_alternative<std::int64_t>(value)) {
        return validateFloat(*def, static_cast<double>(std::get<std::int64_t>(value)));
    }
    if (def->type == SettingType::Int && std::holds_alternative<double>(value)) {
        return validateIntFromFloat(*def, std::get<double>(value));
    }
    if (!holdsStorageFor(def->type, value)) {
        return reject(ValidationStatus::TypeMismatch,
                      def->key + ": expected " + toString(def->type) + ", got " + toDisplayString(value));
    }

    switch (def->type) {
    case SettingType::Int:
        return clampToRange(*def, std::get<std::int64_t>(value));
    case SettingType::Float:
        return validateFloat(*def, std::get<double>(value));
    case SettingType::Enum:
        return validateEnum(*def, std::get<std::string>(value));
    case SettingType::Bool:
    case SettingType::String:
        break;
    }
    return {ValidationStatus::Ok, value, {}};
}

} // namespace evr::settings
