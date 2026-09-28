#pragma once

// A player profile: a base preset plus the settings the player changed (ARCHITECTURE section 12).
//
// Only explicit overrides are stored. Keeping the preset as a live reference rather than copying its
// values means improvements to a preset reach every profile built on it, except for the settings a
// player deliberately changed.
//
// Profiles are loaded from hand-editable files, so overrides are not trusted: every read validates
// them against the schema. An override the schema rejects is ignored in favour of the preset value,
// one it would clamp is read clamped, and one for a key the schema does not know (from an older or
// newer version) is ignored but kept, so saving the profile again does not lose it.
// checkOverrides reports all of these once, when a profile is loaded.

#include "platform/settings/preset.hpp"
#include "platform/settings/schema.hpp"
#include "platform/settings/setting_value.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::settings {

struct Profile {
    std::string name;
    std::string basePreset;
    std::map<std::string, SettingValue, std::less<>> overrides;
};

// Read-only view of everything needed to resolve values.
struct SettingsContext {
    const Schema& schema;
    const PresetRegistry& presets;
};

// The value the profile's preset provides for `key`: the preset's value if it has one, otherwise the
// schema default. Returns nullopt for keys not in the schema. A missing base preset is treated as an
// empty one, so a profile whose preset was removed still resolves to defaults.
std::optional<SettingValue>
presetValue(const SettingsContext& context, const Profile& profile, std::string_view key);

// The effective value: the validated override, else the preset value, else the schema default.
std::optional<SettingValue>
effectiveValue(const SettingsContext& context, const Profile& profile, std::string_view key);

// Validates `value` and stores it as an override if accepted (possibly clamped). The profile is left
// unchanged when the value is rejected. The returned Validation reports what happened.
Validation
setOverride(const Schema& schema, Profile& profile, std::string_view key, const SettingValue& value);

// Removes the override for `key` so it follows the preset again. Returns false if there was none.
bool resetToPreset(Profile& profile, std::string_view key);

void resetAllToPreset(Profile& profile);

// Keys whose effective value differs from what the preset would give, in key order. An override
// equal to the preset value is not a change and is not listed (the launcher should not mark it as
// edited), and neither are overrides that are ignored.
std::vector<std::string> changedFromPreset(const SettingsContext& context, const Profile& profile);

struct OverrideProblem {
    std::string key;
    ValidationStatus status = ValidationStatus::Ok;
    std::string message; // Says what is used instead.
};

// Every override that is not used exactly as stored, in key order: unknown keys (ignored), values
// the schema rejects (the preset value is used) and values it clamps (the clamped value is used).
// For the log and the launcher when a profile is loaded.
std::vector<OverrideProblem> checkOverrides(const Schema& schema, const Profile& profile);

} // namespace evr::settings
