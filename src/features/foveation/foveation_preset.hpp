#pragma once

// Fixed foveation presets (ARCHITECTURE section 11).
//
// A preset sets the angle of the full-rate region around head-forward. The half-rate region reaches from
// there to the same end for every preset (below). Each region is the cone of its angle around head-forward,
// the same directions in both eyes (foveation_region.hpp). Outside the half-rate region shading is at quarter
// rate. Angles are in degrees because that is how they are tuned and discussed; conversion happens at the
// point of use.

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

// Where the half-rate region ends, in degrees from head-forward, the same for every preset: beyond it shading
// is at quarter rate. 56 degrees is past the bottom and the temporal edge of a Quest 3 eye image (54.3 and 54
// degrees), so there only the corners of the image reach quarter rate; the earlier ends of 24 to 46 degrees
// put the whole bottom and the outer edge at quarter rate, where 4x4 blocks showed on shiny floors with DLSS.
// The presets differ in their full-rate angle alone, so the stronger ones still shade less of the image at
// full rate.
namespace half_rate_ends {
inline constexpr float kDegrees = 56.0f;
} // namespace half_rate_ends

// Full-rate half-angle for a preset; nullopt for Off (everything is full rate).
std::optional<float> fullRateHalfAngleDegrees(FoveationPreset preset);

// Width of the half-rate band beyond the full-rate half-angle, up to where the half-rate region ends; nullopt
// for Off.
std::optional<float> halfRateBandDegrees(FoveationPreset preset);

// Pancake lenses stay sharp much further from the centre than fresnel lenses, so reduced shading
// there is more visible. When `gentlerForPancake` is set, the preset moves one notch gentler.
// Subtle stays Subtle: this adjusts strength, it never silently switches off a feature the player
// turned on.
FoveationPreset adjustForLens(FoveationPreset preset, bool gentlerForPancake);

// Parses the settings-file spelling: "off", "subtle", "balanced", "aggressive", "maximum".
std::optional<FoveationPreset> parseFoveationPreset(std::string_view text);

} // namespace evr::foveation
