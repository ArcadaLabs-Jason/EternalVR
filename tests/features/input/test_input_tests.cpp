#include "features/input/test_input.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <ostream>

using evr::Vec3;
using evr::input::applyTestInput;
using evr::input::Axis2;
using evr::input::Hand;
using evr::input::InputFrame;
using evr::input::parseTestInput;
using evr::input::testHandPose;
using evr::test::approxEqual;

TEST_CASE("the file's values lay over the frame; values it leaves out stay the runtime's") {
    const auto input = parseTestInput(R"(# fire and walk forward
right.trigger = 1
left.stick = 0, 1
right.primary = 1
left.menu = 0
)");
    CHECK(input.issues.empty());
    InputFrame frame;
    frame.right.grip = 0.7f;
    frame.left.menuButton = true;
    frame.right.secondaryButton = true;
    applyTestInput(input, frame);
    CHECK(frame.right.trigger == 1.0f);
    CHECK(frame.left.stick == Axis2{0.0f, 1.0f});
    CHECK(frame.right.primaryButton);
    CHECK_FALSE(frame.left.menuButton);
    CHECK(frame.right.grip == 0.7f);
    CHECK(frame.right.secondaryButton);
}

TEST_CASE("an aim makes the hand tracked, pointing where it says") {
    const auto input =
        parseTestInput("right.aim = 90, 0\nleft.aim = 0, 30\nleft.position = -0.1, -0.2, -0.4\n");
    REQUIRE(input.issues.empty());
    InputFrame frame;
    applyTestInput(input, frame);
    REQUIRE(frame.right.poseValid);
    // 90 degrees counter-clockwise from -Z seen from above is -X.
    CHECK(approxEqual(evr::rotate(frame.right.aimPose.orientation, Vec3{0.0f, 0.0f, -1.0f}),
                      Vec3{-1.0f, 0.0f, 0.0f}));
    CHECK(approxEqual(frame.right.aimPose.position, Vec3{0.2f, -0.35f, -0.3f}));
    REQUIRE(frame.left.poseValid);
    const Vec3 up = evr::rotate(frame.left.aimPose.orientation, Vec3{0.0f, 0.0f, -1.0f});
    CHECK(up.y == doctest::Approx(0.5f));
    CHECK(approxEqual(frame.left.aimPose.position, Vec3{-0.1f, -0.2f, -0.4f}));
}

TEST_CASE("a roll turns the hand about its pointing axis") {
    const auto input = parseTestInput("left.aim = 0, 0, 90\n");
    REQUIRE(input.issues.empty());
    InputFrame frame;
    applyTestInput(input, frame);
    REQUIRE(frame.left.gripValid);
    // Still pointing ahead; the controller's right side (the left palm's normal) now faces up.
    CHECK(approxEqual(evr::rotate(frame.left.gripPose.orientation, Vec3{0.0f, 0.0f, -1.0f}),
                      Vec3{0.0f, 0.0f, -1.0f}));
    CHECK(approxEqual(evr::rotate(frame.left.gripPose.orientation, Vec3{1.0f, 0.0f, 0.0f}),
                      Vec3{0.0f, 1.0f, 0.0f}));
    CHECK(parseTestInput("left.aim = 0, 0, 90, 1\n").issues.size() == 1);
}

TEST_CASE("hand positions are measured from the head") {
    const auto input = parseTestInput("right.aim = 0, 0\n");
    InputFrame frame;
    frame.head.poseValid = true;
    frame.head.pose.position = {0.5f, 1.7f, 0.0f};
    applyTestInput(input, frame);
    CHECK(approxEqual(frame.right.aimPose.position, Vec3{0.7f, 1.35f, -0.3f}));
}

TEST_CASE("a hand without an aim keeps the runtime's pose") {
    const auto input = parseTestInput("right.trigger = 0.5\n");
    CHECK_FALSE(testHandPose(input, Hand::Right, {}).has_value());
    InputFrame frame;
    applyTestInput(input, frame);
    CHECK_FALSE(frame.right.poseValid);
}

TEST_CASE("values are clamped to their ranges") {
    const auto input = parseTestInput("right.trigger = 3\nleft.stick = -4, 0.5\n");
    CHECK(input.issues.empty());
    InputFrame frame;
    applyTestInput(input, frame);
    CHECK(frame.right.trigger == 1.0f);
    CHECK(frame.left.stick == Axis2{-1.0f, 0.5f});
}

TEST_CASE("bad lines are reported with their numbers and skipped") {
    const auto input = parseTestInput(R"(right.trigger = 1
middle.trigger = 1
right.stick = 1
right.aim = 0, 120
right.position = 0, 0
right.wave = 1
right.trigger
right.trigger = on
)");
    CHECK(input.issues.size() == 7);
    CHECK(input.issues.front().starts_with("line 2:"));
    CHECK(input.hand(Hand::Right).trigger == 1.0f);
}
