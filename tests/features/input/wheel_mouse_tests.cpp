#include "features/input/turn_stick_arbiter.hpp"
#include "features/input/wheel_mouse.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <ostream>
#include <string_view>

using evr::input::Axis2;
using evr::input::TurnStickArbiter;
using evr::input::WheelDirection;
using evr::input::wheelDirection;
using evr::input::WheelMouse;
using evr::input::WheelMouseOutput;
using evr::input::WheelMouseSettings;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

// Holds the wheel button with the stick at `stick` until the wheel counts as open.
void openWheel(WheelMouse& wheel, Axis2 stick = {}) {
    for (int i = 0; i < 200 && !wheel.open(); ++i) {
        wheel.update(true, stick, kFrame);
    }
    REQUIRE(wheel.open());
}

} // namespace

TEST_CASE("wheel directions: eight segments counter-clockwise from the right") {
    CHECK(wheelDirection({1.0f, 0.0f}, 0.5f) == WheelDirection::Right);
    CHECK(wheelDirection({0.7f, 0.7f}, 0.5f) == WheelDirection::UpRight);
    CHECK(wheelDirection({0.0f, 1.0f}, 0.5f) == WheelDirection::Up);
    CHECK(wheelDirection({-0.7f, 0.7f}, 0.5f) == WheelDirection::UpLeft);
    CHECK(wheelDirection({-1.0f, 0.0f}, 0.5f) == WheelDirection::Left);
    CHECK(wheelDirection({-0.7f, -0.7f}, 0.5f) == WheelDirection::DownLeft);
    CHECK(wheelDirection({0.0f, -1.0f}, 0.5f) == WheelDirection::Down);
    CHECK(wheelDirection({0.7f, -0.7f}, 0.5f) == WheelDirection::DownRight);
    CHECK(wheelDirection({0.2f, -0.2f}, 0.5f) == WheelDirection::None);
    CHECK(wheelDirection({std::numeric_limits<float>::quiet_NaN(), 1.0f}, 0.5f) == WheelDirection::None);
    CHECK(std::string_view(evr::input::wheelDirectionName(WheelDirection::DownLeft)) == "down-left");
}

TEST_CASE("no cursor motion until the wheel has had time to open") {
    WheelMouse wheel;
    const float delay = wheel.settings().openDelaySeconds;
    float held = 0.0f;
    WheelMouseOutput out = wheel.update(true, {0.0f, -1.0f}, kFrame);
    CHECK_FALSE(out.move);
    while (held + kFrame < delay) {
        held += kFrame;
        out = wheel.update(true, {0.0f, -1.0f}, kFrame);
        CHECK_FALSE(out.move);
        CHECK_FALSE(out.opened);
    }
    out = wheel.update(true, {0.0f, -1.0f}, kFrame);
    CHECK(out.opened);
    // The stick that opened it is still down: the cursor goes to the rim straight below the centre.
    CHECK(out.move);
    CHECK(out.dx == 0);
    CHECK(out.dy == 200);
    CHECK(out.pointed == WheelDirection::Down);
    CHECK(out.directionChanged);
}

TEST_CASE("turning the stick moves the cursor from rim point to rim point") {
    WheelMouse wheel;
    openWheel(wheel, {0.0f, -1.0f});
    CHECK(wheel.offset() == Axis2{0.0f, 200.0f});

    // Right: from (0, 200) to (200, 0).
    WheelMouseOutput out = wheel.update(true, {1.0f, 0.0f}, kFrame);
    REQUIRE(out.move);
    CHECK(out.dx == 200);
    CHECK(out.dy == -200);
    CHECK(out.pointed == WheelDirection::Right);
    CHECK(out.directionChanged);
    CHECK(wheel.offset() == Axis2{200.0f, 0.0f});

    // Up-left at part deflection: the direction counts, not the length.
    out = wheel.update(true, {-0.5f, 0.5f}, kFrame);
    REQUIRE(out.move);
    CHECK(out.dx == -341); // (-141.4, -141.4) - (200, 0)
    CHECK(out.dy == -141);
    CHECK(out.pointed == WheelDirection::UpLeft);
    CHECK(std::fabs(std::hypot(wheel.offset().x, wheel.offset().y) - 200.0f) < 1.0f);
}

TEST_CASE("a held direction is pinned to the rim now and then, not every frame") {
    WheelMouseSettings settings;
    WheelMouse wheel(settings);
    openWheel(wheel, {1.0f, 0.0f});
    int moves = 0;
    int pins = 0;
    for (int i = 0; i < 90; ++i) { // one second
        const WheelMouseOutput out = wheel.update(true, {1.0f, 0.001f}, kFrame);
        if (out.move) {
            ++moves;
            CHECK(out.dx == static_cast<int>(settings.pinPixels));
            CHECK(out.dy == 0);
            CHECK_FALSE(out.directionChanged);
            ++pins;
        }
    }
    CHECK(pins >= 8);
    CHECK(pins <= 11);
    CHECK(moves == pins);
    // The model keeps the game's clamp: still on the rim.
    CHECK(wheel.offset().x == doctest::Approx(200.0f));
    CHECK(wheel.offset().y == doctest::Approx(0.0f));
}

TEST_CASE("the stick back near the centre leaves the highlight, and letting go ends the wheel") {
    WheelMouse wheel;
    openWheel(wheel, {0.0f, -1.0f});
    wheel.update(true, {-1.0f, 0.0f}, kFrame);
    const Axis2 atRim = wheel.offset();
    // Springing back through the centre: nothing is sent, the cursor stays on the left.
    for (const Axis2 stick : {Axis2{-0.4f, 0.0f}, Axis2{-0.1f, 0.05f}, Axis2{0.0f, 0.0f}}) {
        const WheelMouseOutput out = wheel.update(true, stick, kFrame);
        CHECK_FALSE(out.move);
    }
    CHECK(wheel.offset() == atRim);
    // The button goes up (the arbiter ends the sweep): the game picks the highlighted weapon.
    const WheelMouseOutput out = wheel.update(false, {}, kFrame);
    CHECK(out.released);
    CHECK(out.moves == 2);
    CHECK_FALSE(wheel.open());
    CHECK(wheel.offset() == Axis2{});
    // A later release (nothing held) reports nothing.
    CHECK_FALSE(wheel.update(false, {}, kFrame).released);
}

TEST_CASE("a release before the wheel opened is not a selection") {
    WheelMouse wheel;
    wheel.update(true, {0.0f, -1.0f}, kFrame);
    const WheelMouseOutput out = wheel.update(false, {}, kFrame);
    CHECK_FALSE(out.released);
    CHECK_FALSE(out.move);
}

TEST_CASE("a lost stick and bad time steps send nothing") {
    WheelMouse wheel;
    openWheel(wheel);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(wheel.update(true, {nan, 1.0f}, kFrame).move);
    CHECK_FALSE(wheel.update(true, {}, nan).move);
    CHECK_FALSE(wheel.update(true, {}, -1.0f).move);
    CHECK(wheel.update(true, {0.0f, 1.0f}, nan).move); // a direction still aims, whatever the clock says
}

TEST_CASE("unusable settings fall back to the defaults") {
    WheelMouseSettings bad;
    bad.reachPixels = -5.0f;
    const WheelMouse wheel(bad);
    CHECK(wheel.settings().reachPixels == WheelMouseSettings{}.reachPixels);
    WheelMouseSettings nan;
    nan.openDelaySeconds = std::numeric_limits<float>::quiet_NaN();
    CHECK(WheelMouse(nan).settings().openDelaySeconds == WheelMouseSettings{}.openDelaySeconds);
}

TEST_CASE("the controller scheme end to end: hold down, point, let go") {
    // The turn stick's arbiter opens the wheel on a held down sweep and hands the stick over as the pointer;
    // the wheel mouse turns the pointer into cursor motion; returning to the centre closes the sweep.
    TurnStickArbiter arbiter;
    WheelMouse wheel;
    bool opened = false;
    bool released = false;
    WheelDirection last = WheelDirection::None;
    auto step = [&](Axis2 stick) {
        const auto gesture = arbiter.update(stick, kFrame);
        const WheelMouseOutput out = wheel.update(gesture.downHold, gesture.wheelPointer, kFrame);
        opened = opened || out.opened;
        released = released || out.released;
        if (out.move) {
            last = out.pointed;
        }
    };
    for (int i = 0; i < 60; ++i) { // held down 0.67 s: the arbiter's hold, then the wheel's open delay
        step({0.0f, -1.0f});
    }
    CHECK(opened);
    CHECK(last == WheelDirection::Down);
    for (int i = 0; i < 10; ++i) { // round to the upper right
        step({0.7f, 0.7f});
    }
    CHECK(last == WheelDirection::UpRight);
    CHECK_FALSE(released);
    step({0.3f, 0.3f});
    step({0.0f, 0.0f});
    CHECK(released);
    CHECK(last == WheelDirection::UpRight);
}
