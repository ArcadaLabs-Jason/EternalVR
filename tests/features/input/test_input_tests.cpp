#include "features/input/test_input.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
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

TEST_CASE("the grip's curl is scripted apart from the grip's press") {
    const auto input = parseTestInput("left.grip_curl = 1\nright.grip_curl = 1.5\n");
    CHECK(input.issues.empty());
    InputFrame frame;
    frame.left.grip = 0.3f;
    applyTestInput(input, frame);
    CHECK(frame.left.gripCurl == 1.0f);
    CHECK(frame.left.grip == 0.3f); // the press is left as it is
    CHECK(frame.right.gripCurl == 1.0f);
    CHECK(frame.right.grip == 0.0f);
    CHECK(parseTestInput("left.grip_curl = 0.5, 1\n").issues.size() == 1);
}

TEST_CASE("the extra face buttons and the bumper can be pressed") {
    const auto input = parseTestInput("right.face3 = 1\nleft.face4 = 1\nright.shoulder = 1\n");
    CHECK(input.issues.empty());
    InputFrame frame;
    applyTestInput(input, frame);
    CHECK(frame.right.face3Button);
    CHECK(frame.left.face4Button);
    CHECK(frame.right.shoulderButton);
    CHECK_FALSE(frame.left.face3Button);
    CHECK_FALSE(frame.left.shoulderButton);
}

TEST_CASE("the touch sensors can be touched and let go") {
    const auto input = parseTestInput(
        "left.thumbrest = 1\nright.primary_touch = 1\nright.secondary_touch = 1\nright.thumbrest = 0\n"
        "left.thumbstick_touch = 1\nright.trackpad_touch = 1\n");
    CHECK(input.issues.empty());
    InputFrame frame;
    frame.right.thumbRest = true;
    applyTestInput(input, frame);
    CHECK(frame.left.thumbRest);
    CHECK_FALSE(frame.right.thumbRest);
    CHECK(frame.right.primaryTouch);
    CHECK(frame.right.secondaryTouch);
    CHECK_FALSE(frame.left.primaryTouch);
    CHECK(frame.left.stickTouch);
    CHECK_FALSE(frame.right.stickTouch);
    CHECK(frame.right.trackpadTouch);
    CHECK_FALSE(frame.left.trackpadTouch);
    CHECK(parseTestInput("left.thumbrest = 0, 1\n").issues.size() == 1);
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

TEST_CASE("a velocity moves a hand given an aim; without one the hand is still") {
    InputFrame still;
    still.right.linearVelocity = {1.0f, 1.0f, 1.0f};
    applyTestInput(parseTestInput("right.aim = 0, 0\n"), still);
    CHECK(still.right.velocityValid);
    CHECK(still.right.linearVelocity == Vec3{});

    const auto input = parseTestInput("left.aim = 0, 0\nleft.velocity = 0, -0.5, -3\n");
    REQUIRE(input.issues.empty());
    InputFrame moving;
    applyTestInput(input, moving);
    CHECK(moving.left.velocityValid);
    CHECK(moving.left.linearVelocity == Vec3{0.0f, -0.5f, -3.0f});
    // No aim: the runtime's hand, velocity and all.
    InputFrame runtime;
    applyTestInput(parseTestInput("left.velocity = 0, 0, -3\n"), runtime);
    CHECK_FALSE(runtime.left.velocityValid);
    CHECK(parseTestInput("left.velocity = 0, 0\n").issues.size() == 1);
    CHECK(parseTestInput("left.velocity = 0, 0, -30\n").issues.size() == 1);
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

namespace {

// The aim's yaw (degrees, counter-clockwise from above) of a pose testHandPose gave: the hand points along
// -Z.
float yawOfPose(const evr::Pose& pose) {
    const Vec3 forward = rotate(pose.orientation, Vec3{0.0f, 0.0f, -1.0f});
    return std::atan2(-forward.x, -forward.z) * 180.0f / 3.14159265f;
}

} // namespace

TEST_CASE("a sweep turns the aim's yaw back and forth for its periods, from when the file first had it") {
    auto input = parseTestInput("right.aim = 10, 0\nright.sweep = 30, 0.25, 2\n");
    REQUIRE(input.issues.empty());
    evr::input::startSweeps(input, nullptr, 100.0);
    const auto at = [&](double t) {
        return yawOfPose(*testHandPose(input, Hand::Right, {}, t));
    };
    CHECK(at(100.0) == doctest::Approx(10.0f).epsilon(1e-3));
    CHECK(at(100.0625) == doctest::Approx(40.0f).epsilon(1e-3));  // a quarter period: the amplitude
    CHECK(at(100.1875) == doctest::Approx(-20.0f).epsilon(1e-3)); // three quarters: the other way
    CHECK(at(100.6) == doctest::Approx(10.0f).epsilon(1e-3));     // after its two periods: still at the aim
    CHECK(at(99.9) == doctest::Approx(10.0f).epsilon(1e-3));
    // The file read again with the same sweep (a capture's buttons added): it goes on from its start.
    auto again = parseTestInput("right.aim = 10, 0\nright.sweep = 30, 0.25, 2\nleft.trigger = 1\n");
    evr::input::startSweeps(again, &input, 100.1);
    CHECK(yawOfPose(*testHandPose(again, Hand::Right, {}, 100.0625)) == doctest::Approx(40.0f).epsilon(1e-3));
    // Another sweep starts when it is read.
    auto other = parseTestInput("right.aim = 10, 0\nright.sweep = 20, 0.5, 1\n");
    evr::input::startSweeps(other, &input, 100.2);
    CHECK(yawOfPose(*testHandPose(other, Hand::Right, {}, 100.325)) == doctest::Approx(30.0f).epsilon(1e-3));
}

TEST_CASE("a sweep the file gets wrong is an issue and no sweep") {
    for (const char* line : {"right.sweep = 30, 0.25", "right.sweep = 120, 0.25, 2",
                             "right.sweep = 30, 0.01, 2", "right.sweep = 30, 0.25, 0"}) {
        const auto input = parseTestInput(line);
        CHECK(input.issues.size() == 1);
        CHECK_FALSE(input.hand(Hand::Right).sweep.has_value());
    }
}

TEST_CASE("a hand's noise: new each frame, the same for the same frame, Gaussian of the given size") {
    const auto input = parseTestInput("left.aim = 0, 0\nleft.noise = 0.5, 0.1\nright.aim = 0, 0\n");
    CHECK(input.issues.empty());
    const evr::Pose pose = *testHandPose(input, Hand::Left, {});
    // No noise on the right hand: its pose as given.
    const evr::Pose right = *testHandPose(input, Hand::Right, {});
    CHECK(evr::input::testHandNoise(input, Hand::Right, right, 3).position == right.position);
    CHECK(evr::input::testHandNoise(input, Hand::Right, right, 3).orientation == right.orientation);
    // The same frame, the same noise; another frame, another.
    const evr::Pose a = evr::input::testHandNoise(input, Hand::Left, pose, 7);
    CHECK(a.position == evr::input::testHandNoise(input, Hand::Left, pose, 7).position);
    CHECK_FALSE(a.position == evr::input::testHandNoise(input, Hand::Left, pose, 8).position);
    // Over many frames: each axis's position about 0.5 mm, the turn about 0.1 degrees per axis.
    double sum = 0.0;
    double sumSquares = 0.0;
    double angleSquares = 0.0;
    constexpr int kFrames = 4000;
    for (int n = 0; n < kFrames; ++n) {
        const evr::Pose p = evr::input::testHandNoise(input, Hand::Left, pose, static_cast<std::uint64_t>(n));
        const Vec3 d = p.position - pose.position;
        sum += d.x;
        sumSquares +=
            static_cast<double>(d.x) * d.x + static_cast<double>(d.y) * d.y + static_cast<double>(d.z) * d.z;
        const evr::Quat q = p.orientation * evr::conjugate(pose.orientation);
        const double angle = 2.0 * std::atan2(evr::length(Vec3{q.x, q.y, q.z}), std::fabs(q.w));
        angleSquares += angle * angle;
    }
    CHECK(std::fabs(sum / kFrames) < 0.00003);
    CHECK(std::sqrt(sumSquares / (3.0 * kFrames)) == doctest::Approx(0.0005).epsilon(0.05));
    CHECK(std::sqrt(angleSquares / (3.0 * kFrames)) * 180.0 / 3.14159265 ==
          doctest::Approx(0.1).epsilon(0.05));
    // Out of range or not two numbers: reported.
    CHECK(parseTestInput("left.noise = 60, 0\n").issues.size() == 1);
    CHECK(parseTestInput("left.noise = 1\n").issues.size() == 1);
    CHECK(parseTestInput("left.noise = -1, 0\n").issues.size() == 1);
}
