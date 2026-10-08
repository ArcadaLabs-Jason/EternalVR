#include "features/posture/posture_detector.hpp"

#include <doctest/doctest.h>

#include <initializer_list>
#include <limits>
#include <ostream>

using evr::posture::effectivePosture;
using evr::posture::plausibleHeadHeight;
using evr::posture::Posture;
using evr::posture::postureAtAnchor;
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

TEST_CASE("a height no head can have never decides the posture") {
    CHECK(plausibleHeadHeight(0.5f));
    CHECK(plausibleHeadHeight(1.2f));
    CHECK(plausibleHeadHeight(2.3f));
    CHECK_FALSE(plausibleHeadHeight(0.06f));
    CHECK_FALSE(plausibleHeadHeight(-0.4f));
    CHECK_FALSE(plausibleHeadHeight(2.6f));
    CHECK_FALSE(plausibleHeadHeight(std::numeric_limits<float>::quiet_NaN()));

    PostureDetector fresh;
    CHECK(fresh.update(0.06f) == Posture::Unknown);
    CHECK(fresh.update(2.6f) == Posture::Unknown);
    CHECK(fresh.update(0.5f) == Posture::Seated);

    PostureDetector standing;
    standing.update(1.67f);
    CHECK(standing.update(0.06f) == Posture::Standing);

    PostureDetector seated;
    seated.update(1.0f);
    CHECK(seated.update(2.6f) == Posture::Seated);
    CHECK(seated.update(2.3f) == Posture::Standing);
}

namespace {

// postureAtAnchor's posture, checking whether it was detected.
Posture atAnchor(
    PostureDetector& detector, Posture inForce, std::optional<float> height, bool detect, bool detected) {
    const auto at = postureAtAnchor(detector, inForce, height, detect);
    CHECK(at.detected == detected);
    return at.posture;
}

} // namespace

TEST_CASE("a runtime recenter keeps the posture; the first anchor and the player's recenter detect it") {
    // The player's session: standing at the first anchor, then SteamVR recentered and put its floor at head
    // height (head 0.06 m above it).
    PostureDetector detector;
    const Posture first = atAnchor(detector, Posture::Unknown, 1.67f, true, true);
    CHECK(first == Posture::Standing);
    CHECK(atAnchor(detector, first, 0.06f, false, false) == Posture::Standing);
    // A runtime recenter keeps it even with a floor reading that would be seated.
    CHECK(atAnchor(detector, first, 1.0f, false, false) == Posture::Standing);
    // The player's recenter on the broken floor keeps it too.
    CHECK(atAnchor(detector, first, 0.06f, true, false) == Posture::Standing);
    // The player's recenter on a good floor detects again.
    CHECK(atAnchor(detector, first, 1.1f, true, true) == Posture::Seated);
    CHECK(atAnchor(detector, Posture::Seated, 1.70f, true, true) == Posture::Standing);
    // From a seated posture the detection starts afresh: inside the band the midpoint decides.
    CHECK(atAnchor(detector, Posture::Seated, 1.40f, true, true) == Posture::Standing);
}

TEST_CASE("an anchor without a usable floor") {
    PostureDetector detector;
    // The first anchor on a broken floor: unknown, as without a floor space.
    CHECK(atAnchor(detector, Posture::Unknown, 0.06f, true, false) == Posture::Unknown);
    // No floor space: unknown, as before.
    CHECK(atAnchor(detector, Posture::Unknown, std::nullopt, true, false) == Posture::Unknown);
    // A floor reading missing for a frame keeps the posture in force.
    CHECK(atAnchor(detector, Posture::Seated, std::nullopt, true, false) == Posture::Seated);
}

TEST_CASE("a runtime recenter detects a posture that is still unknown") {
    // The first anchor had a broken floor; the runtime's recenter then reads a good one.
    PostureDetector detector;
    CHECK(atAnchor(detector, Posture::Unknown, 1.15f, false, true) == Posture::Seated);
    CHECK(atAnchor(detector, Posture::Unknown, 1.70f, false, true) == Posture::Standing);
    // Still broken: still unknown.
    CHECK(atAnchor(detector, Posture::Unknown, 0.06f, false, false) == Posture::Unknown);
}
