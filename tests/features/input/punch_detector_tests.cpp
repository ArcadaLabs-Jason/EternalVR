#include "features/input/punch_detector.hpp"

#include "features/input/input_frames.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <numbers>
#include <ostream>

using evr::input::InputFrame;
using evr::input::PunchDetector;
using evr::input::PunchSettings;
using evr::test::restingFrame;
using evr::test::trackedHand;
using evr::test::yawPose;

namespace {

InputFrame rightHandMoving(evr::Vec3 velocity) {
    InputFrame frame = restingFrame();
    frame.right = trackedHand({0.2f, 1.3f, -0.3f}, velocity);
    return frame;
}

} // namespace

TEST_CASE("a hand faster than the threshold toward head-forward punches") {
    PunchDetector detector;
    CHECK_FALSE(detector.update(restingFrame()));
    CHECK(detector.update(rightHandMoving({0.0f, 0.0f, -3.0f})));
}

TEST_CASE("the hand that punched is reported, for its vibration") {
    PunchDetector detector;
    detector.update(restingFrame());
    CHECK(detector.update(rightHandMoving({0.0f, 0.0f, -3.0f})));
    CHECK(detector.punched()[1]);
    CHECK_FALSE(detector.punched()[0]);
    detector.update(restingFrame());
    CHECK_FALSE(detector.punched()[1]);
}

TEST_CASE("the default threshold is 2.8 m/s") {
    PunchDetector detector;
    detector.update(restingFrame());
    CHECK_FALSE(detector.update(rightHandMoving({0.0f, 0.0f, -2.7f})));
    CHECK(detector.update(rightHandMoving({0.0f, 0.0f, -2.9f})));
}

TEST_CASE("fast motion across or away from the view does not punch") {
    PunchDetector detector;
    detector.update(restingFrame());
    CHECK_FALSE(detector.update(rightHandMoving({4.0f, 0.0f, 0.0f})));
    CHECK_FALSE(detector.update(rightHandMoving({0.0f, 4.0f, 0.0f})));
    CHECK_FALSE(detector.update(rightHandMoving({0.0f, 0.0f, 4.0f})));
}

TEST_CASE("forward is the head's forward, not the tracking space's") {
    PunchDetector detector;
    InputFrame frame = rightHandMoving({-3.0f, 0.0f, 0.0f});
    frame.head.pose = yawPose(std::numbers::pi_v<float> / 2.0f, frame.head.pose.position); // Facing -X.
    detector.update(restingFrame());
    CHECK(detector.update(frame));
}

TEST_CASE("one punch per swing") {
    PunchDetector detector;
    detector.update(restingFrame());
    CHECK(detector.update(rightHandMoving({0.0f, 0.0f, -3.0f})));
    CHECK_FALSE(detector.update(rightHandMoving({0.0f, 0.0f, -3.5f})));
    // Slowing to just under the threshold is not enough to re-arm.
    CHECK_FALSE(detector.update(rightHandMoving({0.0f, 0.0f, -2.0f})));
    CHECK_FALSE(detector.update(rightHandMoving({0.0f, 0.0f, -3.0f})));
    // Below half the threshold re-arms.
    CHECK_FALSE(detector.update(rightHandMoving({0.0f, 0.0f, -1.0f})));
    CHECK(detector.update(rightHandMoving({0.0f, 0.0f, -3.0f})));
}

TEST_CASE("either hand can punch") {
    PunchDetector detector;
    detector.update(restingFrame());
    InputFrame frame = restingFrame();
    frame.left = trackedHand({-0.2f, 1.3f, -0.3f}, {0.0f, 0.0f, -3.0f});
    CHECK(detector.update(frame));
}

TEST_CASE("tracking that resumes mid-swing does not punch") {
    PunchDetector detector;
    InputFrame lost = restingFrame();
    lost.right.velocityValid = false;
    detector.update(lost);
    CHECK_FALSE(detector.update(rightHandMoving({0.0f, 0.0f, -3.0f})));
}

TEST_CASE("threshold is clamped to 1.0-4.0 m/s") {
    CHECK(PunchDetector({true, 0.2f, 0.5f}).settings().thresholdMetresPerSecond == 1.0f);
    CHECK(PunchDetector({true, 9.0f, 0.5f}).settings().thresholdMetresPerSecond == 4.0f);

    PunchDetector gentle({true, 1.5f, 0.5f});
    gentle.update(restingFrame());
    CHECK(gentle.update(rightHandMoving({0.0f, 0.0f, -1.6f})));
}

TEST_CASE("disabled never punches") {
    PunchDetector detector({false, 2.8f, 0.5f});
    detector.update(restingFrame());
    CHECK_FALSE(detector.update(rightHandMoving({0.0f, 0.0f, -4.0f})));
}

TEST_CASE("non-finite or out-of-range punch tuning falls back to the defaults") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const PunchSettings defaults;
    CHECK(PunchDetector({true, nan, 0.5f}).settings().thresholdMetresPerSecond ==
          defaults.thresholdMetresPerSecond);
    CHECK(PunchDetector({true, 2.8f, nan}).settings().rearmFraction == defaults.rearmFraction);
    CHECK(PunchDetector({true, 2.8f, -0.5f}).settings().rearmFraction == defaults.rearmFraction);
    CHECK(PunchDetector({true, 2.8f, 3.0f}).settings().rearmFraction == defaults.rearmFraction);

    // With a NaN threshold nothing compared as fast, so punching silently stopped working.
    PunchDetector detector({true, nan, 0.5f});
    detector.update(restingFrame());
    CHECK(detector.update(rightHandMoving({0.0f, 0.0f, -3.0f})));
}

TEST_CASE("a re-arm fraction above one would punch on every frame") {
    // Guarded by falling back to the default: one punch per swing.
    PunchDetector detector({true, 2.8f, 2.0f});
    detector.update(restingFrame());
    CHECK(detector.update(rightHandMoving({0.0f, 0.0f, -3.0f})));
    CHECK_FALSE(detector.update(rightHandMoving({0.0f, 0.0f, -3.0f})));
}
