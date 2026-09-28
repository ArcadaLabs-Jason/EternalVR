#pragma once

// The built-in schema and presets.
//
// The keys here are an initial set covering comfort, rendering and posture. They will grow as
// features land; each feature owns its keys' names and limits, but they are registered here so the
// complete list is reviewable in one file.

#include "platform/settings/preset.hpp"
#include "platform/settings/schema.hpp"

#include <string>
#include <vector>

namespace evr::settings {

namespace keys {
inline constexpr const char* kVignetteStrength = "comfort.vignette_strength";
inline constexpr const char* kTurnMode = "comfort.turn_mode";
inline constexpr const char* kSnapTurnDegrees = "comfort.snap_turn_degrees";
inline constexpr const char* kCinematicFades = "comfort.cinematic_fades";
inline constexpr const char* kFoveationPreset = "render.foveation_preset";
inline constexpr const char* kPostureMode = "posture.mode";
inline constexpr const char* kRuntimeJson = "xr.runtime_json";
} // namespace keys

namespace preset_names {
inline constexpr const char* kComfortable = "Comfortable";
inline constexpr const char* kRecommended = "Recommended";
inline constexpr const char* kAdvanced = "Advanced";
inline constexpr const char* kIntense = "Intense";
} // namespace preset_names

struct BuiltinSettings {
    Schema schema;
    // Comfortable, Recommended, Advanced and Intense, in that order.
    PresetRegistry presets;
    // One message per definition or preset that was refused. Empty unless the tables in builtin.cpp
    // have a bug; a test keeps it empty, and the layer logs anything here as an error.
    std::vector<std::string> errors;
};

BuiltinSettings makeBuiltinSettings();

} // namespace evr::settings
