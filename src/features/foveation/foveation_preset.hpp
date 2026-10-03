#pragma once

// Fixed foveation presets (ARCHITECTURE section 11).
//
// A preset sets the angle of the full-rate region around head-forward and how many degrees more the
// half-rate region has. Each region has the area of the cone of its angle and reaches the same fraction of
// the way to every edge of the eye's image (foveation_region.hpp). Outside the half-rate region shading is
// at quarter rate. Angles are in degrees because that is how they are tuned and discussed; conversion
// happens at the point of use.

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
    Maximum,
};

// Full-rate half-angles in degrees. Starting values, to be tuned on real headsets: 30 degrees keeps
// the reduced-rate area well outside where players look, 18 degrees is near the edge of lens sweet
// spots on current fresnel headsets, 12 degrees is inside them.
namespace half_angles {
inline constexpr float kSubtleDegrees = 30.0f;
inline constexpr float kBalancedDegrees = 24.0f;
inline constexpr float kAggressiveDegrees = 18.0f;
inline constexpr float kMaximumDegrees = 12.0f;
} // namespace half_angles

// Half-rate band widths beyond the full-rate half-angle, in degrees. Maximum's band is narrower, so
// its quarter rate starts at the region of 24 degrees.
namespace half_rate_bands {
inline constexpr float kDefaultDegrees = 16.0f;
inline constexpr float kMaximumDegrees = 12.0f;
} // namespace half_rate_bands

// Full-rate half-angle for a preset; nullopt for Off (everything is full rate).
std::optional<float> fullRateHalfAngleDegrees(FoveationPreset preset);

// Width of the half-rate band beyond the full-rate half-angle; nullopt for Off.
std::optional<float> halfRateBandDegrees(FoveationPreset preset);

// Pancake lenses stay sharp much further from the centre than fresnel lenses, so reduced shading
// there is more visible. When `gentlerForPancake` is set, the preset moves one notch gentler.
// Subtle stays Subtle: this adjusts strength, it never silently switches off a feature the player
// turned on.
FoveationPreset adjustForLens(FoveationPreset preset, bool gentlerForPancake);

// Parses the settings-file spelling: "off", "subtle", "balanced", "aggressive", "maximum".
std::optional<FoveationPreset> parseFoveationPreset(std::string_view text);

} // namespace evr::foveation
