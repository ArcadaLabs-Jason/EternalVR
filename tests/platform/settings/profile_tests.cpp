#include "platform/settings/profile.hpp"

#include "platform/settings/builtin.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

using evr::settings::Profile;
using evr::settings::SettingsContext;
using evr::settings::SettingValue;
using evr::settings::ValidationStatus;
namespace keys = evr::settings::keys;
namespace preset_names = evr::settings::preset_names;

namespace {

struct Fixture {
    evr::settings::BuiltinSettings builtins = evr::settings::makeBuiltinSettings();
    const evr::settings::Schema& schema = builtins.schema;
    SettingsContext context{builtins.schema, builtins.presets};
};

} // namespace

TEST_CASE("effective value prefers override, then preset, then schema default") {
    const Fixture f;
    Profile profile{"Player", preset_names::kComfortable, {}};

    // Comfortable sets the vignette; it does not mention foveation.
    CHECK(effectiveValue(f.context, profile, keys::kVignetteStrength) == SettingValue{0.8});
    CHECK(effectiveValue(f.context, profile, keys::kFoveationPreset) ==
          SettingValue{std::string("balanced")});

    const auto set = setOverride(f.schema, profile, keys::kVignetteStrength, 0.3);
    CHECK(set.status == ValidationStatus::Ok);
    CHECK(effectiveValue(f.context, profile, keys::kVignetteStrength) == SettingValue{0.3});
    CHECK(presetValue(f.context, profile, keys::kVignetteStrength) == SettingValue{0.8});
}

TEST_CASE("unknown keys resolve to nothing") {
    const Fixture f;
    const Profile profile{"Player", preset_names::kRecommended, {}};
    CHECK_FALSE(effectiveValue(f.context, profile, "no.such.key").has_value());
}

TEST_CASE("a profile whose preset is missing falls back to schema defaults") {
    const Fixture f;
    const Profile profile{"Player", "Deleted Preset", {}};
    CHECK(effectiveValue(f.context, profile, keys::kVignetteStrength) == SettingValue{0.5});
}

TEST_CASE("rejected overrides leave the profile unchanged") {
    const Fixture f;
    Profile profile{"Player", preset_names::kRecommended, {}};

    const auto validation = setOverride(f.schema, profile, keys::kTurnMode, std::string("teleport"));
    CHECK(validation.status == ValidationStatus::InvalidChoice);
    CHECK(profile.overrides.empty());
}

TEST_CASE("clamped overrides store the clamped value") {
    const Fixture f;
    Profile profile{"Player", preset_names::kRecommended, {}};

    const auto validation = setOverride(f.schema, profile, keys::kSnapTurnDegrees, std::int64_t{180});
    CHECK(validation.status == ValidationStatus::Clamped);
    CHECK(effectiveValue(f.context, profile, keys::kSnapTurnDegrees) == SettingValue{std::int64_t{90}});
}

TEST_CASE("reset one and reset all restore preset values") {
    const Fixture f;
    Profile profile{"Player", preset_names::kAdvanced, {}};
    setOverride(f.schema, profile, keys::kVignetteStrength, 0.9);
    setOverride(f.schema, profile, keys::kCinematicFades, true);

    CHECK(evr::settings::resetToPreset(profile, keys::kVignetteStrength));
    CHECK_FALSE(evr::settings::resetToPreset(profile, keys::kVignetteStrength));
    CHECK(effectiveValue(f.context, profile, keys::kVignetteStrength) == SettingValue{0.2});
    CHECK(effectiveValue(f.context, profile, keys::kCinematicFades) == SettingValue{true});

    evr::settings::resetAllToPreset(profile);
    CHECK(profile.overrides.empty());
    CHECK(effectiveValue(f.context, profile, keys::kCinematicFades) == SettingValue{false});
}

TEST_CASE("changed-from-preset lists only overrides that differ") {
    const Fixture f;
    Profile profile{"Player", preset_names::kComfortable, {}};
    setOverride(f.schema, profile, keys::kVignetteStrength, 0.8);                    // same as preset
    setOverride(f.schema, profile, keys::kSnapTurnDegrees, std::int64_t{30});        // differs
    setOverride(f.schema, profile, keys::kPostureMode, std::string("seated"));       // differs from default
    setOverride(f.schema, profile, keys::kFoveationPreset, std::string("balanced")); // same as default

    // Listed in key order.
    const std::vector<std::string> expected{keys::kSnapTurnDegrees, keys::kPostureMode};
    CHECK(changedFromPreset(f.context, profile) == expected);
}

TEST_CASE("switching the base preset keeps overrides") {
    const Fixture f;
    Profile profile{"Player", preset_names::kComfortable, {}};
    setOverride(f.schema, profile, keys::kSnapTurnDegrees, std::int64_t{60});

    profile.basePreset = preset_names::kIntense;
    CHECK(effectiveValue(f.context, profile, keys::kSnapTurnDegrees) == SettingValue{std::int64_t{60}});
    CHECK(effectiveValue(f.context, profile, keys::kVignetteStrength) == SettingValue{0.0});
}

TEST_CASE("a loaded override of the wrong type falls back to the preset value") {
    const Fixture f;
    // As a settings file could hold it: never passed through setOverride.
    Profile profile{"Player", preset_names::kRecommended, {}};
    profile.overrides[keys::kVignetteStrength] = std::string("oops");
    profile.overrides[keys::kTurnMode] = std::string("teleport");
    profile.overrides[keys::kSnapTurnDegrees] = std::int64_t{500};

    CHECK(effectiveValue(f.context, profile, keys::kVignetteStrength) == SettingValue{0.5});
    CHECK(effectiveValue(f.context, profile, keys::kTurnMode) == SettingValue{std::string("snap")});
    // Out of range is clamped, as setOverride would have done.
    CHECK(effectiveValue(f.context, profile, keys::kSnapTurnDegrees) == SettingValue{std::int64_t{90}});
    // Loaded values that are merely spelled differently are converted.
    profile.overrides[keys::kSnapTurnDegrees] = 60.0;
    CHECK(effectiveValue(f.context, profile, keys::kSnapTurnDegrees) == SettingValue{std::int64_t{60}});
}

TEST_CASE("changed-from-preset ignores unknown keys and ignored overrides") {
    const Fixture f;
    Profile profile{"Player", preset_names::kRecommended, {}};
    profile.overrides["comfort.removed_key"] = 1.0;
    profile.overrides[keys::kVignetteStrength] = std::string("oops");
    profile.overrides[keys::kSnapTurnDegrees] = std::int64_t{500};
    CHECK(changedFromPreset(f.context, profile) == std::vector<std::string>{keys::kSnapTurnDegrees});
}

TEST_CASE("checking overrides reports what is not used as stored") {
    const Fixture f;
    Profile profile{"Player", preset_names::kRecommended, {}};
    profile.overrides["comfort.removed_key"] = 1.0;
    profile.overrides[keys::kVignetteStrength] = std::string("oops");
    profile.overrides[keys::kSnapTurnDegrees] = std::int64_t{500};
    profile.overrides[keys::kCinematicFades] = false;

    const auto problems = evr::settings::checkOverrides(f.schema, profile);
    REQUIRE(problems.size() == 3);
    CHECK(problems[0].key == "comfort.removed_key");
    CHECK(problems[0].status == ValidationStatus::UnknownKey);
    CHECK(problems[1].key == keys::kSnapTurnDegrees);
    CHECK(problems[1].status == ValidationStatus::Clamped);
    CHECK(problems[2].key == keys::kVignetteStrength);
    CHECK(problems[2].status == ValidationStatus::TypeMismatch);
    CHECK(problems[2].message.find("preset value") != std::string::npos);
    // Unknown keys are kept, so a file from a newer version keeps them when saved again.
    CHECK(profile.overrides.contains("comfort.removed_key"));
}

TEST_CASE("presets are validated when registered") {
    const Fixture f;
    evr::settings::PresetRegistry presets;
    using evr::settings::PresetError;

    const auto unknown = presets.add(f.schema, {"Typo", {{"comfort.vignete_strength", 0.5}}});
    REQUIRE_FALSE(unknown.ok());
    CHECK(unknown.error().code == PresetError::UnknownKey);

    const auto wrongType = presets.add(f.schema, {"Wrong", {{keys::kVignetteStrength, std::string("high")}}});
    REQUIRE_FALSE(wrongType.ok());
    CHECK(wrongType.error().code == PresetError::InvalidValue);

    const auto outOfRange = presets.add(f.schema, {"Far", {{keys::kVignetteStrength, 3.0}}});
    REQUIRE_FALSE(outOfRange.ok());
    CHECK(outOfRange.error().code == PresetError::InvalidValue);
    CHECK(presets.names().empty());

    // Accepted values are stored in the schema's type.
    const auto added = presets.add(f.schema, {"Good", {{keys::kVignetteStrength, std::int64_t{1}}}});
    REQUIRE(added.ok());
    CHECK((*added)->values.at(keys::kVignetteStrength) == SettingValue{1.0});

    const auto again = presets.add(f.schema, {"Good", {}});
    REQUIRE_FALSE(again.ok());
    CHECK(again.error().code == PresetError::NameTaken);
}
