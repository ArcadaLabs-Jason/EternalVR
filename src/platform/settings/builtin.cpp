#include "platform/settings/builtin.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace evr::settings {

namespace {

// Refusals are collected rather than asserted, so a bad table fails a test in every build type
// instead of disappearing from release builds.
class Registrar {
public:
    explicit Registrar(BuiltinSettings& settings) : settings_(settings) {}

    void define(SettingDef def) {
        const std::string key = def.key;
        if (!settings_.schema.add(std::move(def))) {
            settings_.errors.push_back("built-in setting '" + key +
                                       "' is duplicated or has an invalid default");
        }
    }

    void preset(Preset preset) {
        const auto added = settings_.presets.add(settings_.schema, std::move(preset));
        if (!added) {
            settings_.errors.push_back("built-in " + added.error().message);
        }
    }

private:
    BuiltinSettings& settings_;
};

std::int64_t integer(int value) {
    return std::int64_t{value};
}

void defineSchema(Registrar& r) {
    r.define({
        .key = keys::kVignetteStrength,
        .type = SettingType::Float,
        .defaultValue = 0.5,
        .min = 0.0,
        .max = 1.0,
        .choices = {},
        .liveTunable = true,
        .description = "Strength of the comfort vignette during fast movement. 0 disables it.",
    });
    r.define({
        .key = keys::kTurnMode,
        .type = SettingType::Enum,
        .defaultValue = std::string("snap"),
        .min = std::nullopt,
        .max = std::nullopt,
        .choices = {"snap", "smooth"},
        .liveTunable = true,
        .description = "How the turn stick rotates the view.",
    });
    r.define({
        .key = keys::kSnapTurnDegrees,
        .type = SettingType::Int,
        .defaultValue = integer(45),
        .min = 15.0,
        .max = 90.0,
        .choices = {},
        .liveTunable = true,
        .description = "Angle of one snap turn, in degrees.",
    });
    r.define({
        .key = keys::kCinematicFades,
        .type = SettingType::Bool,
        .defaultValue = true,
        .min = std::nullopt,
        .max = std::nullopt,
        .choices = {},
        .liveTunable = true,
        .description = "Fade briefly around forced camera moves such as glory kills.",
    });
    r.define({
        .key = keys::kFoveationPreset,
        .type = SettingType::Enum,
        .defaultValue = std::string("balanced"),
        .min = std::nullopt,
        .max = std::nullopt,
        .choices = {"off", "subtle", "balanced", "aggressive"},
        .liveTunable = true,
        .description = "Fixed foveated rendering strength.",
    });
    r.define({
        .key = keys::kPostureMode,
        .type = SettingType::Enum,
        .defaultValue = std::string("auto"),
        .min = std::nullopt,
        .max = std::nullopt,
        .choices = {"auto", "seated", "standing"},
        .liveTunable = false,
        .description = "Seated or standing play. Auto detects it from the headset height.",
    });
    r.define({
        .key = keys::kRuntimeJson,
        .type = SettingType::String,
        .defaultValue = std::string(),
        .min = std::nullopt,
        .max = std::nullopt,
        .choices = {},
        .liveTunable = false,
        .description = "OpenXR runtime manifest to launch with. Empty uses the system default runtime.",
    });
}

void definePresets(Registrar& r) {
    // Presets differ only in comfort settings for now. Values not listed come from the schema.
    // String values are spelled std::string(...) on purpose: standard libraries predating P0608
    // convert a bare string literal to the variant's bool alternative.
    r.preset({preset_names::kComfortable,
              {
                  {keys::kVignetteStrength, 0.8},
                  {keys::kTurnMode, std::string("snap")},
                  {keys::kSnapTurnDegrees, integer(45)},
                  {keys::kCinematicFades, true},
              }});
    r.preset({preset_names::kRecommended,
              {
                  {keys::kVignetteStrength, 0.5},
                  {keys::kTurnMode, std::string("snap")},
                  {keys::kSnapTurnDegrees, integer(30)},
                  {keys::kCinematicFades, true},
              }});
    r.preset({preset_names::kAdvanced,
              {
                  {keys::kVignetteStrength, 0.2},
                  {keys::kTurnMode, std::string("smooth")},
                  {keys::kCinematicFades, false},
              }});
    r.preset({preset_names::kIntense,
              {
                  {keys::kVignetteStrength, 0.0},
                  {keys::kTurnMode, std::string("smooth")},
                  {keys::kCinematicFades, false},
                  {keys::kFoveationPreset, std::string("aggressive")},
              }});
}

} // namespace

BuiltinSettings makeBuiltinSettings() {
    BuiltinSettings settings;
    Registrar registrar(settings);
    defineSchema(registrar);
    definePresets(registrar);
    return settings;
}

} // namespace evr::settings
