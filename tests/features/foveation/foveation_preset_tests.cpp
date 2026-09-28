#include "features/foveation/foveation_preset.hpp"

#include <doctest/doctest.h>
#include <ostream>

using evr::foveation::adjustForLens;
using evr::foveation::FoveationPreset;
using evr::foveation::fullRateHalfAngleDegrees;
using evr::foveation::parseFoveationPreset;

TEST_CASE("presets map to their full-rate half-angles") {
    CHECK_FALSE(fullRateHalfAngleDegrees(FoveationPreset::Off).has_value());
    CHECK(fullRateHalfAngleDegrees(FoveationPreset::Subtle) == 30.0f);
    CHECK(fullRateHalfAngleDegrees(FoveationPreset::Balanced) == 24.0f);
    CHECK(fullRateHalfAngleDegrees(FoveationPreset::Aggressive) == 18.0f);
}

TEST_CASE("stronger presets have smaller full-rate regions") {
    CHECK(*fullRateHalfAngleDegrees(FoveationPreset::Subtle) >
          *fullRateHalfAngleDegrees(FoveationPreset::Balanced));
    CHECK(*fullRateHalfAngleDegrees(FoveationPreset::Balanced) >
          *fullRateHalfAngleDegrees(FoveationPreset::Aggressive));
}

TEST_CASE("pancake adjustment moves one notch gentler but never turns foveation off") {
    CHECK(adjustForLens(FoveationPreset::Aggressive, true) == FoveationPreset::Balanced);
    CHECK(adjustForLens(FoveationPreset::Balanced, true) == FoveationPreset::Subtle);
    CHECK(adjustForLens(FoveationPreset::Subtle, true) == FoveationPreset::Subtle);
    CHECK(adjustForLens(FoveationPreset::Off, true) == FoveationPreset::Off);
}

TEST_CASE("without the pancake adjustment presets are unchanged") {
    CHECK(adjustForLens(FoveationPreset::Aggressive, false) == FoveationPreset::Aggressive);
    CHECK(adjustForLens(FoveationPreset::Off, false) == FoveationPreset::Off);
}

TEST_CASE("presets parse from their settings spelling") {
    CHECK(parseFoveationPreset("off") == FoveationPreset::Off);
    CHECK(parseFoveationPreset("subtle") == FoveationPreset::Subtle);
    CHECK(parseFoveationPreset("balanced") == FoveationPreset::Balanced);
    CHECK(parseFoveationPreset("aggressive") == FoveationPreset::Aggressive);
    CHECK_FALSE(parseFoveationPreset("Balanced").has_value());
    CHECK_FALSE(parseFoveationPreset("").has_value());
}
