#include "features/foveation/foveation_preset.hpp"

#include <doctest/doctest.h>
#include <ostream>

using evr::foveation::adjustForLens;
using evr::foveation::FoveationPreset;
using evr::foveation::fullRateHalfAngleDegrees;
using evr::foveation::halfRateBandDegrees;
using evr::foveation::parseFoveationPreset;

TEST_CASE("presets map to their full-rate half-angles") {
    CHECK_FALSE(fullRateHalfAngleDegrees(FoveationPreset::Off).has_value());
    CHECK(fullRateHalfAngleDegrees(FoveationPreset::Subtle) == 30.0f);
    CHECK(fullRateHalfAngleDegrees(FoveationPreset::Balanced) == 24.0f);
    CHECK(fullRateHalfAngleDegrees(FoveationPreset::Aggressive) == 18.0f);
    CHECK(fullRateHalfAngleDegrees(FoveationPreset::Maximum) == 12.0f);
}

TEST_CASE("presets map to their half-rate bands") {
    CHECK_FALSE(halfRateBandDegrees(FoveationPreset::Off).has_value());
    CHECK(halfRateBandDegrees(FoveationPreset::Subtle) == 16.0f);
    CHECK(halfRateBandDegrees(FoveationPreset::Balanced) == 16.0f);
    CHECK(halfRateBandDegrees(FoveationPreset::Aggressive) == 16.0f);
    CHECK(halfRateBandDegrees(FoveationPreset::Maximum) == 12.0f);
    // Maximum's quarter rate starts at 24 degrees.
    CHECK(*fullRateHalfAngleDegrees(FoveationPreset::Maximum) +
              *halfRateBandDegrees(FoveationPreset::Maximum) ==
          24.0f);
}

TEST_CASE("stronger presets have smaller full-rate regions") {
    CHECK(*fullRateHalfAngleDegrees(FoveationPreset::Subtle) >
          *fullRateHalfAngleDegrees(FoveationPreset::Balanced));
    CHECK(*fullRateHalfAngleDegrees(FoveationPreset::Balanced) >
          *fullRateHalfAngleDegrees(FoveationPreset::Aggressive));
    CHECK(*fullRateHalfAngleDegrees(FoveationPreset::Aggressive) >
          *fullRateHalfAngleDegrees(FoveationPreset::Maximum));
}

TEST_CASE("pancake adjustment moves one notch gentler but never turns foveation off") {
    CHECK(adjustForLens(FoveationPreset::Maximum, true) == FoveationPreset::Aggressive);
    CHECK(adjustForLens(FoveationPreset::Aggressive, true) == FoveationPreset::Balanced);
    CHECK(adjustForLens(FoveationPreset::Balanced, true) == FoveationPreset::Subtle);
    CHECK(adjustForLens(FoveationPreset::Subtle, true) == FoveationPreset::Subtle);
    CHECK(adjustForLens(FoveationPreset::Off, true) == FoveationPreset::Off);
}

TEST_CASE("without the pancake adjustment presets are unchanged") {
    CHECK(adjustForLens(FoveationPreset::Maximum, false) == FoveationPreset::Maximum);
    CHECK(adjustForLens(FoveationPreset::Aggressive, false) == FoveationPreset::Aggressive);
    CHECK(adjustForLens(FoveationPreset::Off, false) == FoveationPreset::Off);
}

TEST_CASE("presets parse from their settings spelling") {
    CHECK(parseFoveationPreset("off") == FoveationPreset::Off);
    CHECK(parseFoveationPreset("subtle") == FoveationPreset::Subtle);
    CHECK(parseFoveationPreset("balanced") == FoveationPreset::Balanced);
    CHECK(parseFoveationPreset("aggressive") == FoveationPreset::Aggressive);
    CHECK(parseFoveationPreset("maximum") == FoveationPreset::Maximum);
    CHECK_FALSE(parseFoveationPreset("Maximum").has_value());
    CHECK_FALSE(parseFoveationPreset("Balanced").has_value());
    CHECK_FALSE(parseFoveationPreset("").has_value());
}
