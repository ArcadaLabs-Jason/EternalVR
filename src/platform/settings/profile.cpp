#include "platform/settings/profile.hpp"

#include <utility>

namespace evr::settings {

std::optional<SettingValue>
presetValue(const SettingsContext& context, const Profile& profile, std::string_view key) {
    const SettingDef* def = context.schema.find(key);
    if (def == nullptr) {
        return std::nullopt;
    }
    if (const Preset* preset = context.presets.find(profile.basePreset)) {
        if (const auto it = preset->values.find(key); it != preset->values.end()) {
            return it->second;
        }
    }
    return def->defaultValue;
}

std::optional<SettingValue>
effectiveValue(const SettingsContext& context, const Profile& profile, std::string_view key) {
    if (context.schema.find(key) == nullptr) {
        return std::nullopt;
    }
    if (const auto it = profile.overrides.find(key); it != profile.overrides.end()) {
        if (Validation validation = context.schema.validate(key, it->second); validation.accepted()) {
            return std::move(validation.value);
        }
    }
    return presetValue(context, profile, key);
}

Validation
setOverride(const Schema& schema, Profile& profile, std::string_view key, const SettingValue& value) {
    Validation validation = schema.validate(key, value);
    if (validation.accepted()) {
        profile.overrides.insert_or_assign(std::string(key), validation.value);
    }
    return validation;
}

bool resetToPreset(Profile& profile, std::string_view key) {
    const auto it = profile.overrides.find(key);
    if (it == profile.overrides.end()) {
        return false;
    }
    profile.overrides.erase(it);
    return true;
}

void resetAllToPreset(Profile& profile) {
    profile.overrides.clear();
}

std::vector<std::string> changedFromPreset(const SettingsContext& context, const Profile& profile) {
    std::vector<std::string> changed;
    for (const auto& entry : profile.overrides) {
        const std::string& key = entry.first;
        const std::optional<SettingValue> preset = presetValue(context, profile, key);
        if (preset && effectiveValue(context, profile, key) != preset) {
            changed.push_back(key);
        }
    }
    return changed;
}

std::vector<OverrideProblem> checkOverrides(const Schema& schema, const Profile& profile) {
    std::vector<OverrideProblem> problems;
    for (const auto& [key, value] : profile.overrides) {
        const Validation validation = schema.validate(key, value);
        switch (validation.status) {
        case ValidationStatus::Ok:
            break;
        case ValidationStatus::Clamped:
            problems.push_back({key, validation.status, validation.message});
            break;
        case ValidationStatus::UnknownKey:
            problems.push_back({key, validation.status, validation.message + "; it is ignored"});
            break;
        case ValidationStatus::TypeMismatch:
        case ValidationStatus::InvalidChoice:
        case ValidationStatus::NotFinite:
            problems.push_back({key, validation.status, validation.message + "; the preset value is used"});
            break;
        }
    }
    return problems;
}

} // namespace evr::settings
