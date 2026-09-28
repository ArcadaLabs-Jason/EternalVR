#include "features/input/tap_hold.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <ostream>

using evr::input::TapHoldDetector;
using evr::input::TapHoldOutput;

namespace {

constexpr float kFrame = 0.01f;

} // namespace

TEST_CASE("a short press is a tap on release") {
    TapHoldDetector detector(0.25f);
    for (int i = 0; i < 10; ++i) {
        const TapHoldOutput output = detector.update(true, kFrame);
        CHECK_FALSE(output.tap);
        CHECK_FALSE(output.hold);
    }
    CHECK(detector.update(false, kFrame).tap);
    CHECK_FALSE(detector.update(false, kFrame).tap);
}

TEST_CASE("a long press is a hold, and its release is not a tap") {
    TapHoldDetector detector(0.25f);
    detector.update(true, kFrame); // Time zero.
    bool held = false;
    for (int i = 0; i < 30; ++i) {
        held = detector.update(true, kFrame).hold;
    }
    CHECK(held);
    const TapHoldOutput release = detector.update(false, kFrame);
    CHECK_FALSE(release.tap);
    CHECK_FALSE(release.hold);
}

TEST_CASE("hold starts exactly at the hold time") {
    TapHoldDetector detector(0.25f);
    detector.update(true, 0.1f);
    CHECK_FALSE(detector.update(true, 0.1f).hold); // 0.1 s held.
    CHECK_FALSE(detector.update(true, 0.1f).hold); // 0.2 s.
    CHECK(detector.update(true, 0.1f).hold);       // 0.3 s.
}

TEST_CASE("each press is timed from its own start") {
    TapHoldDetector detector(0.25f);
    detector.update(true, kFrame);
    detector.update(true, 0.2f);
    detector.update(false, kFrame);
    detector.update(true, kFrame);
    CHECK_FALSE(detector.update(true, 0.1f).hold);
}

TEST_CASE("an unusable hold time falls back to the default") {
    CHECK(TapHoldDetector(std::numeric_limits<float>::quiet_NaN()).holdSeconds() ==
          evr::input::kDefaultHoldSeconds);
    CHECK(TapHoldDetector(-1.0f).holdSeconds() == evr::input::kDefaultHoldSeconds);
    CHECK(TapHoldDetector(0.4f).holdSeconds() == 0.4f);
}

TEST_CASE("a longer tap time keeps a release after the hold time a tap") {
    TapHoldDetector detector(0.25f, 1.0f);
    detector.update(true, kFrame);
    CHECK(detector.update(true, 0.5f).hold); // the hold action runs meanwhile
    CHECK(detector.update(false, kFrame).tap);
    // Held past the tap time: the hold completed, no tap on release.
    detector.update(true, kFrame);
    detector.update(true, 1.1f);
    CHECK_FALSE(detector.update(false, kFrame).tap);
}

TEST_CASE("a tap time below the hold time or unusable falls back to the hold time") {
    CHECK(TapHoldDetector(0.25f, 0.1f).tapSeconds() == 0.25f);
    CHECK(TapHoldDetector(0.25f, std::numeric_limits<float>::infinity()).tapSeconds() == 0.25f);
    CHECK(TapHoldDetector(0.25f).tapSeconds() == 0.25f);
}

TEST_CASE("a cancelled press is neither a hold nor a tap, and the next press is ordinary") {
    TapHoldDetector detector(0.25f, 1.0f);
    detector.update(true, kFrame);
    CHECK(detector.update(true, 0.3f).hold);
    detector.cancel();
    CHECK_FALSE(detector.update(true, kFrame).hold);
    CHECK_FALSE(detector.update(false, kFrame).tap); // released before the tap time: would have been a tap
    detector.update(true, kFrame);
    CHECK(detector.update(true, 0.3f).hold);
    CHECK(detector.update(false, kFrame).tap);
}

TEST_CASE("a cancel on the frame the press starts covers that press") {
    TapHoldDetector detector(0.25f);
    detector.cancel();
    detector.update(true, kFrame);
    CHECK_FALSE(detector.update(false, kFrame).tap);
}

TEST_CASE("a cancel while the button is up is dropped") {
    TapHoldDetector detector(0.25f);
    detector.update(false, kFrame);
    detector.cancel();
    detector.update(false, kFrame);
    detector.update(true, kFrame);
    CHECK(detector.update(false, kFrame).tap);
}
