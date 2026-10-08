#include "features/posture/seated_walk.hpp"

#include <doctest/doctest.h>

#include <ostream>

using evr::posture::kSeatedWalkSeconds;
using evr::posture::SeatedWalk;

namespace {

constexpr double kFrame = 1.0 / 90.0;

// Feeds the same frame for `seconds` at 90 Hz; returns when it fired (seconds from `from`), or -1.
double feed(SeatedWalk& walk, bool seated, float metres, double from, double seconds) {
    for (double t = from; t < from + seconds; t += kFrame) {
        if (walk.update(seated, metres, t)) {
            return t - from;
        }
    }
    return -1.0;
}

} // namespace

TEST_CASE("walking while seated fires after the wait") {
    SeatedWalk walk;
    CHECK(feed(walk, true, 1.4f, 0.0, 5.0) == doctest::Approx(kSeatedWalkSeconds).epsilon(0.01));
    // It starts over.
    CHECK(walk.since() < 0.0);
}

TEST_CASE("a seated lean, a short walk or a posture that does not block never fires") {
    SeatedWalk lean;
    CHECK(feed(lean, true, 0.8f, 0.0, 30.0) < 0.0);
    SeatedWalk brief;
    CHECK(feed(brief, true, 1.4f, 0.0, 1.5) < 0.0);
    CHECK(feed(brief, true, 0.5f, 1.5, 0.1) < 0.0); // back near the seat: the wait starts again
    CHECK(feed(brief, true, 1.4f, 1.6, 1.5) < 0.0);
    SeatedWalk standing;
    CHECK(feed(standing, false, 3.0f, 0.0, 30.0) < 0.0);
}

TEST_CASE("time going backwards restarts the wait; reset forgets it") {
    SeatedWalk walk;
    CHECK_FALSE(walk.update(true, 1.4f, 10.0));
    CHECK_FALSE(walk.update(true, 1.4f, 5.0));
    CHECK_FALSE(walk.update(true, 1.4f, 6.9));
    CHECK(walk.update(true, 1.4f, 7.0));
    CHECK_FALSE(walk.update(true, 1.4f, 8.0));
    walk.reset();
    CHECK_FALSE(walk.update(true, 1.4f, 9.5));
    CHECK(walk.since() == doctest::Approx(9.5));
}
