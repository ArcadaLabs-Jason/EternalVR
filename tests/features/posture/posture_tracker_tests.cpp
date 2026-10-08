#include "features/posture/posture_tracker.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <ostream>

using evr::posture::Posture;
using evr::posture::PostureChange;
using evr::posture::PostureTracker;
using evr::posture::PostureTrackerSettings;

namespace {

constexpr double kFrame = 1.0 / 90.0;

// Feeds `height` from `from` for `seconds` at 90 Hz; returns the first change and when it came.
struct Fed {
    std::optional<PostureChange> change;
    double at = -1.0;
};

Fed feed(PostureTracker& tracker, float height, double from, double seconds) {
    Fed fed;
    for (double t = from; t < from + seconds; t += kFrame) {
        if (auto change = tracker.update(height, t)) {
            if (!fed.change) {
                fed.change = change;
                fed.at = t - from;
            }
        }
    }
    return fed;
}

} // namespace

TEST_CASE("sit to stand: the owner's seated anchor at 0.96 m, then standing at 1.66 m for a second") {
    PostureTracker tracker;
    tracker.reset(Posture::Seated, 0.962f);
    // Seated for a while: nothing.
    CHECK_FALSE(feed(tracker, 0.97f, 0.0, 5.0).change);
    // Rising: changing before the dwell starts.
    CHECK_FALSE(tracker.update(1.20f, 5.0));
    CHECK(tracker.changing());
    CHECK_FALSE(tracker.pending());
    const Fed stood = feed(tracker, 1.66f, 5.1, 3.0);
    REQUIRE(stood.change);
    CHECK(stood.change->from == Posture::Seated);
    CHECK(stood.change->to == Posture::Standing);
    CHECK(stood.change->heightMetres == doctest::Approx(1.66f));
    CHECK(stood.at == doctest::Approx(1.0).epsilon(0.02));
    CHECK(tracker.current() == Posture::Standing);
    CHECK_FALSE(tracker.changing());
    CHECK_FALSE(tracker.pending());
}

TEST_CASE("stand to sit takes 1.5 s below the seated threshold") {
    PostureTracker tracker;
    tracker.reset(Posture::Standing, 1.65f);
    const Fed sat = feed(tracker, 1.10f, 0.0, 3.0);
    REQUIRE(sat.change);
    CHECK(sat.change->to == Posture::Seated);
    CHECK(sat.at == doctest::Approx(1.5).epsilon(0.02));
    // And back up again, now referenced to the seated height.
    const Fed stood = feed(tracker, 1.60f, 3.0, 2.0);
    REQUIRE(stood.change);
    CHECK(stood.change->to == Posture::Standing);
}

TEST_CASE("a brief crouch or a brief rise does not flip the posture") {
    PostureTracker standing;
    standing.reset(Posture::Standing, 1.70f);
    // Ducking to 1.0 m for 1.2 s, three times, standing in between.
    double t = 0.0;
    for (int i = 0; i < 3; ++i) {
        CHECK_FALSE(feed(standing, 1.00f, t, 1.2).change);
        CHECK(standing.pending());
        CHECK_FALSE(feed(standing, 1.70f, t + 1.2, 1.0).change);
        CHECK_FALSE(standing.pending());
        t += 2.2;
    }
    CHECK(standing.current() == Posture::Standing);

    PostureTracker seated;
    seated.reset(Posture::Seated, 1.00f);
    // Half standing to reach for something, 0.8 s.
    CHECK_FALSE(feed(seated, 1.50f, 0.0, 0.8).change);
    CHECK_FALSE(feed(seated, 1.00f, 0.8, 2.0).change);
    CHECK(seated.current() == Posture::Seated);
}

TEST_CASE("sitting up straight near the top of the seated band is not standing") {
    // A tall player detected seated at 1.30 m sits up to 1.45 m: over the standing threshold, but less
    // than the minimum change from where seated was detected.
    PostureTracker tracker;
    tracker.reset(Posture::Seated, 1.30f);
    CHECK_FALSE(feed(tracker, 1.45f, 0.0, 5.0).change);
    // Really standing: 1.85 m.
    CHECK(feed(tracker, 1.85f, 5.0, 2.0).change);
}

TEST_CASE("an unknown posture is detected once the floor has read the head for a second") {
    // No floor at the anchor (a WMR headset logged 'head (no floor space)' while STAGE existed).
    PostureTracker noHeight;
    noHeight.reset(Posture::Seated, std::nullopt);
    CHECK(noHeight.current() == Posture::Unknown);
    // No reading, or none a head can have: nothing.
    for (double t = 0.0; t < 5.0; t += kFrame) {
        CHECK_FALSE(noHeight.update(std::nullopt, t));
        CHECK_FALSE(noHeight.update(0.06f, t));
    }
    const Fed stood = feed(noHeight, 1.75f, 5.0, 3.0);
    REQUIRE(stood.change);
    CHECK(stood.change->from == Posture::Unknown);
    CHECK(stood.change->to == Posture::Standing);
    CHECK(stood.at == doctest::Approx(1.0).epsilon(0.02));
    CHECK(noHeight.current() == Posture::Standing);
    // Then tracked as usual from that height.
    CHECK(feed(noHeight, 1.0f, 8.0, 3.0).change);
    CHECK(noHeight.current() == Posture::Seated);

    PostureTracker unknown;
    unknown.reset(Posture::Unknown, 1.0f);
    const Fed sat = feed(unknown, 1.1f, 0.0, 3.0);
    REQUIRE(sat.change);
    CHECK(sat.change->to == Posture::Seated);
    // Readings that cross between the ranges restart the second.
    PostureTracker unsure;
    unsure.reset(Posture::Unknown, std::nullopt);
    for (double t = 0.0; t < 5.0; t += 0.5) {
        CHECK_FALSE(feed(unsure, (static_cast<int>(t * 2.0) % 2 == 0) ? 1.1f : 1.6f, t, 0.5).change);
    }
}

TEST_CASE("missing samples up to 0.25 s neither count nor break a dwell; time going backwards restarts it") {
    PostureTracker tracker;
    tracker.reset(Posture::Seated, 1.0f);
    CHECK_FALSE(tracker.update(1.7f, 0.0));
    CHECK_FALSE(tracker.update(std::nullopt, 0.1));
    CHECK_FALSE(tracker.update(std::nanf(""), 0.2));
    CHECK_FALSE(tracker.update(1.7f, 0.25));
    CHECK_FALSE(tracker.update(std::nullopt, 0.4));
    CHECK_FALSE(tracker.update(1.7f, 0.5));
    CHECK_FALSE(tracker.update(1.7f, 0.75));
    CHECK(tracker.update(1.7f, 1.0));

    PostureTracker back;
    back.reset(Posture::Seated, 1.0f);
    CHECK_FALSE(back.update(1.7f, 10.0));
    CHECK_FALSE(back.update(1.7f, 5.0)); // backwards: the dwell starts again at 5.0
    CHECK_FALSE(feed(back, 1.7f, 5.0 + kFrame, 0.9).change);
    CHECK(back.update(1.7f, 6.0));
}

TEST_CASE("a floor no head can have is no reading") {
    // A player's SteamVR recenter put the floor at head height: standing, the head read 0.06 m above it.
    PostureTracker standing;
    standing.reset(Posture::Standing, 1.67f);
    CHECK_FALSE(feed(standing, 0.06f, 0.0, 10.0).change);
    CHECK(standing.current() == Posture::Standing);
    CHECK_FALSE(standing.changing());
    CHECK_FALSE(standing.pending());
    // Seated under a floor far below: no stand-up either.
    PostureTracker seated;
    seated.reset(Posture::Seated, 1.0f);
    CHECK_FALSE(feed(seated, 2.6f, 0.0, 10.0).change);
    CHECK(seated.current() == Posture::Seated);
    // Like a missing sample, it does not count toward a dwell, and a long run of them starts it again: one
    // reading before and one after never flip the posture at once.
    PostureTracker dwell;
    dwell.reset(Posture::Standing, 1.7f);
    CHECK_FALSE(dwell.update(1.0f, 0.0));
    CHECK_FALSE(dwell.update(0.06f, 1.0));
    CHECK(dwell.pending());
    CHECK_FALSE(dwell.update(1.0f, 1.6));
    CHECK(dwell.pending());
    CHECK_FALSE(feed(dwell, 1.0f, 1.6 + kFrame, 1.4).change);
    CHECK(dwell.update(1.0f, 3.11));
    // The edges of the range are heads.
    PostureTracker low;
    low.reset(Posture::Standing, 1.7f);
    CHECK(feed(low, 0.5f, 0.0, 2.0).change);
}

TEST_CASE("a reset re-references the height (a recenter while standing)") {
    PostureTracker tracker;
    tracker.reset(Posture::Seated, 1.0f);
    CHECK_FALSE(tracker.update(1.7f, 0.0));
    tracker.reset(Posture::Standing, 1.7f);
    CHECK_FALSE(tracker.pending());
    CHECK_FALSE(feed(tracker, 1.7f, 0.0, 5.0).change);
}

TEST_CASE("bad settings fall back to the defaults") {
    PostureTrackerSettings bad;
    bad.seatedBelowMetres = 1.6f; // above standing
    PostureTracker tracker(bad);
    CHECK(tracker.settings().seatedBelowMetres ==
          doctest::Approx(PostureTrackerSettings{}.seatedBelowMetres));
    PostureTrackerSettings nan;
    nan.standingSeconds = std::nanf("");
    CHECK(PostureTracker(nan).settings().standingSeconds == doctest::Approx(1.0f));
}
