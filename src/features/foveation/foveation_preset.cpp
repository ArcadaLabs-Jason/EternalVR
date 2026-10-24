#include "features/foveation/foveation_preset.hpp"

namespace evr::foveation {

std::optional<float> fullRateHalfAngleDegrees(FoveationPreset preset) {
    switch (preset) {
    case FoveationPreset::Off:
        return std::nullopt;
    case FoveationPreset::Subtle:
        return half_angles::kSubtleDegrees;
    case FoveationPreset::Balanced:
        return half_angles::kBalancedDegrees;
    case FoveationPreset::Aggressive:
        return half_angles::kAggressiveDegrees;
    case FoveationPreset::Maximum:
        return half_angles::kMaximumDegrees;
    }
    return std::nullopt;
}

std::optional<float> halfRateBandDegrees(FoveationPreset preset) {
    const std::optional<float> full = fullRateHalfAngleDegrees(preset);
    if (!full) {
        return std::nullopt;
    }
    return half_rate_ends::kDegrees - *full;
}

FoveationPreset adjustForLens(FoveationPreset preset, bool gentlerForPancake) {
    if (!gentlerForPancake) {
        return preset;
    }
    switch (preset) {
    case FoveationPreset::Maximum:
        return FoveationPreset::Aggressive;
    case FoveationPreset::Aggressive:
        return FoveationPreset::Balanced;
    case FoveationPreset::Balanced:
        return FoveationPreset::Subtle;
    case FoveationPreset::Subtle:
    case FoveationPreset::Off:
        break;
    }
    return preset;
}

std::optional<FoveationPreset> parseFoveationPreset(std::string_view text) {
    if (text == "off") {
        return FoveationPreset::Off;
    }
    if (text == "subtle") {
        return FoveationPreset::Subtle;
    }
    if (text == "balanced") {
        return FoveationPreset::Balanced;
    }
    if (text == "aggressive") {
        return FoveationPreset::Aggressive;
    }
    if (text == "maximum") {
        return FoveationPreset::Maximum;
    }
    return std::nullopt;
}

} // namespace evr::foveation
