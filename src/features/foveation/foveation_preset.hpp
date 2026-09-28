#pragma once

// Fixed foveation presets (ARCHITECTURE section 11).
//
// A preset sets the half-angle of the full-rate region: the cone around head-forward that is shaded
// at full rate. Outside it the shading rate drops. Angles are in degrees because that is how they
// are tuned and discussed; conversion happens at the point of use.

#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::foveation {

// Ordered from gentlest to strongest; the lens adjustment below relies on that order.
enum class FoveationPreset : std::uint8_t {
    Off,
    Subtle,
    Balanced,
    Aggressive,
};

// Full-rate half-angles in degrees. Starting values, to be tuned on real headsets: 30 degrees keeps
// the reduced-rate area well outside where players look, 18 degrees is near the edge of lens sweet
// spots on current fresnel headsets.
namespace half_angles {
inline constexpr float kSubtleDegrees = 30.0f;
inline constexpr float kBalancedDegrees = 24.0f;
inline constexpr float kAggressiveDegrees = 18.0f;
} // namespace half_angles

// Full-rate half-angle for a preset; nullopt for Off (everything is full rate).
std::optional<float> fullRateHalfAngleDegrees(FoveationPreset preset);

// Pancake lenses stay sharp much further from the centre than fresnel lenses, so reduced shading
// there is more visible. When `gentlerForPancake` is set, the preset moves one notch gentler.
// Subtle stays Subtle: this adjusts strength, it never silently switches off a feature the player
// turned on.
FoveationPreset adjustForLens(FoveationPreset preset, bool gentlerForPancake);

// Parses the settings-file spelling: "off", "subtle", "balanced", "aggressive".
std::optional<FoveationPreset> parseFoveationPreset(std::string_view text);

} // namespace evr::foveation
