#include "features/input/hands_jump.hpp"

#include "features/input/input_frames.hpp"

#include <doctest/doctest.h>
#include <ostream>

using evr::input::HandsJumpDetector;
using evr::input::HandsJumpSettings;
using evr::input::InputFrame;
using evr::posture::Posture;
using evr::test::kHeadHeight;
using evr::test::restingFrame;
using evr::test::trackedHand;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

HandsJumpSettings enabled() {
    HandsJumpSettings settings;
    settings.enabled = true;
    return settings;
}

// Both hands at `height`, moving up at `speed`.
InputFrame handsAt(float height, float speed) {
    InputFrame frame = restingFrame();
    frame.left = trackedHand({-0.2f, height, -0.2f}, {0.0f, speed, 0.0f});
    frame.right = trackedHand({0.2f, height, -0.2f}, {0.0f, speed, 0.0f});
    return frame;
}

// Rest, then a fast two-hand throw up past the head. Returns the number of jumps fired.
int throwHandsUp(HandsJumpDetector& detector, Posture posture) {
    int jumps = 0;
    jumps += detector.update(restingFrame(), posture, kFrame) ? 1 : 0;
    jumps += detector.update(handsAt(1.5f, 2.5f), posture, kFrame) ? 1 : 0;
    jumps += detector.update(handsAt(kHeadHeight + 0.05f, 2.2f), posture, kFrame) ? 1 : 0;
    jumps += detector.update(handsAt(kHeadHeight + 0.15f, 0.8f), posture, kFrame) ? 1 : 0;
    return jumps;
}

} // namespace

TEST_CASE("off by default") {
    HandsJumpDetector detector;
    CHECK(throwHandsUp(detector, Posture::Standing) == 0);
}

TEST_CASE("both hands thrown up past the head jump once") {
    HandsJumpDetector detector(enabled());
    CHECK(throwHandsUp(detector, Posture::Standing) == 1);
}

TEST_CASE("fires when the hands reach the head just after the fast part") {
    HandsJumpDetector detector(enabled());
    detector.update(restingFrame(), Posture::Standing, kFrame);
    CHECK_FALSE(detector.update(handsAt(1.6f, 2.5f), Posture::Standing, kFrame));
    // Slowed at full extension, but fast within the window.
    CHECK(detector.update(handsAt(kHeadHeight + 0.1f, 0.6f), Posture::Standing, kFrame));
}

TEST_CASE("fast hands below the head do not jump") {
    HandsJumpDetector detector(enabled());
    detector.update(restingFrame(), Posture::Standing, kFrame);
    CHECK_FALSE(detector.update(handsAt(1.4f, 3.0f), Posture::Standing, kFrame));
    CHECK_FALSE(detector.update(handsAt(1.5f, 3.0f), Posture::Standing, kFrame));
}

TEST_CASE("slowly raised hands do not jump") {
    HandsJumpDetector detector(enabled());
    detector.update(restingFrame(), Posture::Standing, kFrame);
    CHECK_FALSE(detector.update(handsAt(kHeadHeight + 0.1f, 1.0f), Posture::Standing, kFrame));
}

TEST_CASE("one hand alone does not jump") {
    HandsJumpDetector detector(enabled());
    detector.update(restingFrame(), Posture::Standing, kFrame);
    InputFrame frame = handsAt(kHeadHeight + 0.1f, 2.5f);
    frame.left = trackedHand({-0.2f, 1.2f, -0.3f});
    CHECK_FALSE(detector.update(frame, Posture::Standing, kFrame));
}

TEST_CASE("blocked while seated unless allowed") {
    HandsJumpDetector seated(enabled());
    CHECK(throwHandsUp(seated, Posture::Seated) == 0);

    HandsJumpSettings allow = enabled();
    allow.allowWhenSeated = true;
    HandsJumpDetector seatedAllowed(allow);
    CHECK(throwHandsUp(seatedAllowed, Posture::Seated) == 1);

    HandsJumpDetector unknown(enabled());
    CHECK(throwHandsUp(unknown, Posture::Unknown) == 1);
}

TEST_CASE("hands must settle before the next jump") {
    HandsJumpDetector detector(enabled());
    CHECK(throwHandsUp(detector, Posture::Standing) == 1);
    // Pumping up again without settling, well after the cooldown.
    int jumps = 0;
    for (int i = 0; i < 60; ++i) {
        jumps += detector.update(handsAt(kHeadHeight + 0.1f, 2.5f), Posture::Standing, kFrame) ? 1 : 0;
    }
    CHECK(jumps == 0);
    CHECK(throwHandsUp(detector, Posture::Standing) == 1);
}

TEST_CASE("tracking that resumes mid-throw does not jump") {
    HandsJumpDetector detector(enabled());
    InputFrame lost = restingFrame();
    lost.left.poseValid = false;
    detector.update(lost, Posture::Standing, kFrame);
    CHECK_FALSE(detector.update(handsAt(kHeadHeight + 0.1f, 2.5f), Posture::Standing, kFrame));
}
