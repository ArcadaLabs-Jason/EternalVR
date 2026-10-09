#include "features/tracking/fov_watch.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <ostream>
#include <string>

using evr::tracking::EyeFovs;
using evr::tracking::FovProblem;
using evr::tracking::fovProblem;
using evr::tracking::FovWatch;
using Outcome = evr::tracking::FovWatch::Outcome;

namespace {

constexpr float kRadians = 1.0f / 57.29578f;

// An eye in degrees (left and down negative, as XrFovf has them); eye R mirrors eye L.
EyeFovs eyes(float left, float right, float up, float down) {
    const evr::xr_math::Fov l{left * kRadians, right * kRadians, up * kRadians, down * kRadians};
    const evr::xr_math::Fov r{-right * kRadians, -left * kRadians, up * kRadians, down * kRadians};
    return {l, r};
}

// The same eye with every tangent scaled by `multiplier` (Quest Link's FOV tangent multiplier).
EyeFovs scaled(float left, float right, float up, float down, float multiplier) {
    const auto angle = [multiplier](float degrees) {
        return std::atan(std::tan(degrees * kRadians) * multiplier) / kRadians;
    };
    return eyes(angle(left), angle(right), angle(up), angle(down));
}

// Quest 3 through VDXR (docs/VR_HEAD_TRACKED.md): 54/40 across, 44 up, 55 down.
const EyeFovs kQuest3 = eyes(-54.0f, 40.0f, 44.0f, -55.0f);
// Virtual Desktop's first read on a Quest 3 in a player log, and a Rift S at 29 x 32 for both eyes.
const EyeFovs kVirtualDesktopRead = eyes(-15.0f, 40.0f, 14.0f, -26.0f);
const EyeFovs kRiftSRead = eyes(-14.5f, 14.5f, 16.0f, -16.0f);

} // namespace

TEST_CASE("headsets' FOVs are plausible") {
    CHECK(fovProblem(kQuest3) == FovProblem::None);
    CHECK(fovProblem(eyes(-45.0f, 45.0f, 45.0f, -45.0f)) == FovProblem::None); // the OpenXR Simulator
    CHECK(fovProblem(eyes(-55.0f, 50.0f, 55.0f, -55.0f)) == FovProblem::None);
    CHECK(fovProblem(eyes(-65.0f, 40.0f, 50.0f, -50.0f)) == FovProblem::None); // a canted wide headset
    CHECK(fovProblem(eyes(-85.0f, 45.0f, 55.0f, -55.0f)) == FovProblem::None); // a Pimax's outer side
}

TEST_CASE("a FOV narrowed by Quest Link's tangent multiplier is plausible") {
    // Quest 2 (about 52/41 across, 45 up, 52 down) at 0.6 and 0.5; Quest 3 at 0.55.
    CHECK(fovProblem(scaled(-52.0f, 41.0f, 45.0f, -52.0f, 0.6f)) == FovProblem::None);
    CHECK(fovProblem(scaled(-52.0f, 41.0f, 45.0f, -52.0f, 0.5f)) == FovProblem::None);
    CHECK(fovProblem(scaled(-54.0f, 40.0f, 44.0f, -55.0f, 0.55f)) == FovProblem::None);
}

TEST_CASE("the FOVs seen in player logs are not, except half of a usual one") {
    CHECK(fovProblem(kVirtualDesktopRead) == FovProblem::Narrow);
    CHECK(fovProblem(kRiftSRead) == FovProblem::Narrow);
    // Half of the usual FOV (a Steam Frame session) passes: it is the runtime's own (below, the real one
    // takes over once it is read).
    CHECK(fovProblem(eyes(-30.0f, 29.0f, 30.0f, -30.0f)) == FovProblem::None);
}

TEST_CASE("a first read at half the FOV gives way to the real one within a few reads") {
    FovWatch watch;
    const EyeFovs half = eyes(-30.0f, 29.0f, 30.0f, -30.0f);
    const EyeFovs full = eyes(-60.0f, 58.0f, 60.0f, -60.0f);
    CHECK(watch.onRead(half, 0.0) == Outcome::First);
    double t = FovWatch::kReadSeconds;
    CHECK(watch.onRead(full, t) == Outcome::Waiting);
    for (int i = 2; i < FovWatch::kStableReads; ++i) {
        t += FovWatch::kRetrySeconds;
        CHECK(watch.onRead(full, t) == Outcome::Waiting);
    }
    t += FovWatch::kRetrySeconds;
    CHECK(watch.onRead(full, t) == Outcome::Changed);
    CHECK(t <= FovWatch::kReadSeconds + 1.0);
    CHECK(evr::tracking::sameFovs(*watch.taken(), full, 0.01f));
}

TEST_CASE("each kind of implausible FOV is named") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(fovProblem(eyes(nan, 40.0f, 44.0f, -55.0f)) == FovProblem::NotFinite);
    CHECK(fovProblem(eyes(-5.0f, 70.0f, 44.0f, -55.0f)) == FovProblem::Edge);
    CHECK(fovProblem(eyes(-88.0f, 40.0f, 44.0f, -55.0f)) == FovProblem::Edge);
    CHECK(fovProblem(eyes(-60.0f, 20.0f, 44.0f, -55.0f)) == FovProblem::Lopsided);
    EyeFovs differ = kQuest3;
    differ[1].angleRight = 30.0f * kRadians;
    CHECK(fovProblem(differ) == FovProblem::EyesDiffer);
    CHECK(std::string(evr::tracking::fovProblemText(FovProblem::Narrow)).size() > 0);
}

TEST_CASE("the first plausible read is taken at once; an implausible one never") {
    FovWatch watch;
    CHECK(watch.due(0.0));
    CHECK(watch.onRead(kVirtualDesktopRead, 0.0) == Outcome::Implausible);
    CHECK_FALSE(watch.taken());
    // Read again soon while nothing is taken.
    CHECK_FALSE(watch.due(0.1));
    CHECK(watch.due(FovWatch::kRetrySeconds));
    CHECK(watch.onRead(kQuest3, 0.3) == Outcome::First);
    REQUIRE(watch.taken());
    CHECK(evr::tracking::sameFovs(*watch.taken(), kQuest3, 0.01f));
    // Then every kReadSeconds.
    CHECK_FALSE(watch.due(0.3 + FovWatch::kReadSeconds - 0.01));
    CHECK(watch.due(0.3 + FovWatch::kReadSeconds));
    // A later implausible read keeps the FOV taken.
    CHECK(watch.onRead(kRiftSRead, 2.3) == Outcome::Implausible);
    CHECK(evr::tracking::sameFovs(*watch.taken(), kQuest3, 0.01f));
    CHECK(watch.implausibleReads() == 2);
}

TEST_CASE("a different FOV is taken once it holds, a passing one never") {
    FovWatch watch;
    CHECK(watch.onRead(kQuest3, 0.0) == Outcome::First);
    const EyeFovs wider = eyes(-56.0f, 42.0f, 46.0f, -56.0f);
    CHECK(watch.onRead(wider, 2.0) == Outcome::Waiting);
    CHECK(watch.due(2.0 + FovWatch::kRetrySeconds));          // read again soon while a change waits
    CHECK(watch.onRead(kQuest3, 2.25) == Outcome::Unchanged); // it went back: the change starts over
    CHECK(watch.onRead(wider, 2.5) == Outcome::Waiting);
    CHECK(watch.onRead(wider, 2.75) == Outcome::Waiting);
    CHECK(watch.onRead(wider, 3.0) == Outcome::Changed);
    CHECK(evr::tracking::sameFovs(*watch.taken(), wider, 0.01f));
    CHECK(watch.changes() == 1);
    // A wobble within kSameDegrees is no change.
    CHECK(watch.onRead(eyes(-56.3f, 42.2f, 46.0f, -56.0f), 5.0) == Outcome::Unchanged);
}

TEST_CASE("an implausible read during a change neither counts for it nor stops it") {
    FovWatch watch;
    watch.onRead(kQuest3, 0.0);
    const EyeFovs wider = eyes(-56.0f, 42.0f, 46.0f, -56.0f);
    CHECK(watch.onRead(wider, 2.0) == Outcome::Waiting);
    CHECK(watch.onRead(kVirtualDesktopRead, 2.25) == Outcome::Implausible);
    CHECK(watch.onRead(wider, 2.5) == Outcome::Waiting);
    CHECK(watch.onRead(wider, 2.75) == Outcome::Changed);
}

TEST_CASE("without the check every finite read counts; reset forgets everything") {
    FovWatch watch;
    CHECK(watch.onRead(kRiftSRead, 0.0, false) == Outcome::First);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(watch.onRead(eyes(nan, 40.0f, 44.0f, -55.0f), 2.0, false) == Outcome::Implausible);
    watch.reset();
    CHECK_FALSE(watch.taken());
    CHECK(watch.due(0.0));
    CHECK(watch.implausibleReads() == 0);
}

TEST_CASE("after many implausible reads in a row the reads slow down to the usual interval") {
    FovWatch watch;
    double t = 0.0;
    for (int i = 0; i < FovWatch::kRetryReads - 1; ++i) {
        CHECK(watch.onRead(kRiftSRead, t) == Outcome::Implausible);
        CHECK(watch.due(t + FovWatch::kRetrySeconds));
        t += FovWatch::kRetrySeconds;
    }
    CHECK(watch.onRead(kRiftSRead, t) == Outcome::Implausible);
    CHECK_FALSE(watch.due(t + FovWatch::kRetrySeconds));
    CHECK(watch.due(t + FovWatch::kReadSeconds));
    // A plausible read is taken whenever it comes.
    CHECK(watch.onRead(kQuest3, t + FovWatch::kReadSeconds) == Outcome::First);
}
