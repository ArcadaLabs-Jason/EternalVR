#include "features/posture/eye_height.hpp"

#include <doctest/doctest.h>
#include <ostream>

using evr::posture::eyeHeightOffset;

namespace {

constexpr float kCharacterEyeHeight = 1.65f;

} // namespace

TEST_CASE("offset lifts a freshly anchored head to the character's eye height") {
    CHECK(eyeHeightOffset(0.0f, kCharacterEyeHeight) == doctest::Approx(kCharacterEyeHeight));
}

TEST_CASE("offset accounts for where the head was when the anchor was taken") {
    // Seated and standing players both end up at the same virtual eye height.
    const float seatedHead = 1.18f;
    const float standingHead = 1.72f;
    CHECK(seatedHead + eyeHeightOffset(seatedHead, kCharacterEyeHeight) ==
          doctest::Approx(kCharacterEyeHeight));
    CHECK(standingHead + eyeHeightOffset(standingHead, kCharacterEyeHeight) ==
          doctest::Approx(kCharacterEyeHeight));
}

TEST_CASE("custom target eye height") {
    CHECK(eyeHeightOffset(0.1f, 1.8f) == doctest::Approx(1.7));
}
