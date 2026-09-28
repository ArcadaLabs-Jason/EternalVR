#include "features/posture/posture_detector.hpp"

#include <doctest/doctest.h>

#include <initializer_list>
#include <limits>
#include <ostream>

using evr::posture::effectivePosture;
using evr::posture::Posture;
using evr::posture::PostureDetector;
using evr::posture::PostureOverride;
using evr::posture::PostureThresholds;

TEST_CASE("clear heights classify immediately") {
    PostureDetector seated;
    CHECK(seated.update(1.15f) == Posture::Seated);

    PostureDetector standing;
    CHECK(standing.update(1.70f) == Posture::Standing);
}

TEST_CASE("unknown height stays unknown until a height is seen") {
    PostureDetector detector;
    CHECK(detector.update(std::nullopt) == Posture::Unknown);
    CHECK(detector.current() == Posture::Unknown);
}

TEST_CASE("losing the height keeps the last posture") {
    PostureDetector detector;
    detector.update(1.75f);
    CHECK(detector.update(std::nullopt) == Posture::Standing);
}

TEST_CASE("hysteresis band keeps the current posture") {
    PostureDetector detector;
    detector.update(1.20f);
    // Seated player sitting up tall: inside the band, still seated.
    CHECK(detector.update(1.42f) == Posture::Seated);
    // Stands up: crosses the upper threshold.
    CHECK(detector.update(1.50f) == Posture::Standing);
    // Crouches a little: inside the band, still standing.
    CHECK(detector.update(1.32f) == Posture::Standing);
    // Sits down: crosses the lower threshold.
    CHECK(detector.update(1.25f) == Posture::Seated);
}

TEST_CASE("first sample inside the band splits at the midpoint") {
    // Default band is 1.30-1.45 m, midpoint 1.375 m.
    PostureDetector low;
    CHECK(low.update(1.35f) == Posture::Seated);

    PostureDetector high;
    CHECK(high.update(1.40f) == Posture::Standing);
}

TEST_CASE("custom thresholds are honoured") {
    PostureDetector detector({1.0f, 1.2f});
    CHECK(detector.update(1.25f) == Posture::Standing);
    CHECK(detector.update(1.1f) == Posture::Standing);
    CHECK(detector.update(0.9f) == Posture::Seated);
}

TEST_CASE("reset forgets the detected posture") {
    PostureDetector detector;
    detector.update(1.75f);
    detector.reset();
    CHECK(detector.current() == Posture::Unknown);
}

TEST_CASE("player override wins over detection") {
    CHECK(effectivePosture(PostureOverride::Auto, Posture::Standing) == Posture::Standing);
    CHECK(effectivePosture(PostureOverride::Auto, Posture::Unknown) == Posture::Unknown);
    CHECK(effectivePosture(PostureOverride::Seated, Posture::Standing) == Posture::Seated);
    CHECK(effectivePosture(PostureOverride::Standing, Posture::Unknown) == Posture::Standing);
}

TEST_CASE("non-finite heights are ignored like unknown ones") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    PostureDetector fresh;
    CHECK(fresh.update(nan) == Posture::Unknown);
    CHECK(fresh.update(inf) == Posture::Unknown);
    CHECK(fresh.update(-inf) == Posture::Unknown);

    PostureDetector seated;
    seated.update(1.0f);
    CHECK(seated.update(inf) == Posture::Seated);
    CHECK(seated.update(nan) == Posture::Seated);

    PostureDetector standing;
    standing.update(1.8f);
    CHECK(standing.update(-inf) == Posture::Standing);
}

TEST_CASE("invalid thresholds fall back to the defaults") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const PostureThresholds defaults;
    for (const PostureThresholds& bad :
         {PostureThresholds{1.5f, 1.2f}, PostureThresholds{1.3f, 1.3f}, PostureThresholds{nan, 1.45f},
          PostureThresholds{1.3f, nan}, PostureThresholds{-1.0f, 1.45f}, PostureThresholds{1.3f, 40.0f}}) {
        const PostureDetector detector(bad);
        CHECK(detector.thresholds().seatedBelowMetres == defaults.seatedBelowMetres);
        CHECK(detector.thresholds().standingAboveMetres == defaults.standingAboveMetres);
    }
    // Valid custom thresholds are kept.
    CHECK(PostureDetector({1.0f, 1.2f}).thresholds().standingAboveMetres == 1.2f);
}
