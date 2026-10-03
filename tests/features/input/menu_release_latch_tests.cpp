#include "features/input/menu_release_latch.hpp"

#include "features/input/input_frames.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <ostream>

// Inputs held when a menu's hold on gameplay ends: each reads as released (a stick as centred) until it is
// let go, while a fresh press passes at once.

using evr::input::Axis2;
using evr::input::InputFrame;
using evr::input::MenuReleaseLatch;
using evr::input::MenuReleaseOutput;
using evr::test::restingFrame;

namespace {

InputFrame withLeftSecondary(bool down) {
    InputFrame frame = restingFrame();
    frame.left.secondaryButton = down;
    return frame;
}

InputFrame withRightStick(Axis2 stick) {
    InputFrame frame = restingFrame();
    frame.right.stick = stick;
    return frame;
}

} // namespace

TEST_CASE("frames under the hold pass unchanged; the first frame after it is marked") {
    MenuReleaseLatch latch;
    InputFrame frame = withLeftSecondary(true);
    frame.right.trigger = 1.0f;
    frame.right.stick = {0.0f, -1.0f};
    const MenuReleaseOutput held = latch.update(frame, true);
    CHECK(held.frame.left.secondaryButton);
    CHECK(held.frame.right.trigger == 1.0f);
    CHECK(held.frame.right.stick == Axis2{0.0f, -1.0f});
    CHECK_FALSE(held.holdEnded);
    CHECK(latch.update(restingFrame(), false).holdEnded);
    CHECK_FALSE(latch.update(restingFrame(), false).holdEnded);
}

TEST_CASE("B held when the menu closes reads as released until it is let go, then presses again") {
    MenuReleaseLatch latch;
    latch.update(withLeftSecondary(true), true);
    for (int i = 0; i < 20; ++i) {
        CHECK_FALSE(latch.update(withLeftSecondary(true), false).frame.left.secondaryButton);
    }
    CHECK(latch.anyLatched());
    CHECK_FALSE(latch.update(withLeftSecondary(false), false).frame.left.secondaryButton);
    CHECK_FALSE(latch.anyLatched());
    CHECK(latch.update(withLeftSecondary(true), false).frame.left.secondaryButton);
}

TEST_CASE("a stick held when the menu closes reads as centred until it comes back") {
    MenuReleaseLatch latch;
    latch.update(withRightStick({0.0f, -1.0f}), true);
    CHECK(latch.update(withRightStick({0.0f, -1.0f}), false).frame.right.stick == Axis2{});
    // Easing back is still out until within the centre.
    CHECK(latch.update(withRightStick({0.0f, -0.4f}), false).frame.right.stick == Axis2{});
    CHECK(latch.update(withRightStick({0.0f, 1.0f}), false).frame.right.stick == Axis2{});
    CHECK(latch.update(withRightStick({0.1f, 0.0f}), false).frame.right.stick == Axis2{0.1f, 0.0f});
    CHECK(latch.update(withRightStick({0.0f, -1.0f}), false).frame.right.stick == Axis2{0.0f, -1.0f});
}

TEST_CASE("a press made after the menu closed passes at once") {
    MenuReleaseLatch latch;
    latch.update(restingFrame(), true);
    InputFrame frame = withLeftSecondary(true);
    frame.right.trigger = 1.0f;
    frame.left.stick = {0.0f, 1.0f};
    const MenuReleaseOutput out = latch.update(frame, false);
    CHECK(out.holdEnded);
    CHECK(out.frame.left.secondaryButton);
    CHECK(out.frame.right.trigger == 1.0f);
    CHECK(out.frame.left.stick == Axis2{0.0f, 1.0f});
    CHECK_FALSE(latch.anyLatched());
}

TEST_CASE("a press made and let go in the menu is not latched") {
    MenuReleaseLatch latch;
    latch.update(withLeftSecondary(true), true);
    latch.update(withLeftSecondary(false), true);
    CHECK_FALSE(latch.anyLatched());
    latch.update(restingFrame(), false);
    CHECK(latch.update(withLeftSecondary(true), false).frame.left.secondaryButton);
}

TEST_CASE("an input let go on the very frame the hold ends is not latched") {
    MenuReleaseLatch latch;
    latch.update(withLeftSecondary(true), true);
    const MenuReleaseOutput out = latch.update(withLeftSecondary(false), false);
    CHECK(out.holdEnded);
    CHECK_FALSE(latch.anyLatched());
    CHECK(latch.update(withLeftSecondary(true), false).frame.left.secondaryButton);
}

TEST_CASE("several inputs held through the close are each let go on their own") {
    MenuReleaseLatch latch;
    InputFrame held = restingFrame();
    held.right.trigger = 1.0f;
    held.right.grip = 1.0f;
    held.left.secondaryButton = true;
    held.left.menuButton = true;
    held.right.face3Button = true;
    held.right.stick = {0.0f, 1.0f};
    latch.update(held, true);

    MenuReleaseOutput out = latch.update(held, false);
    CHECK(out.frame.right.trigger == 0.0f);
    CHECK(out.frame.right.grip == 0.0f);
    CHECK_FALSE(out.frame.left.secondaryButton);
    CHECK_FALSE(out.frame.left.menuButton);
    CHECK_FALSE(out.frame.right.face3Button);
    CHECK(out.frame.right.stick == Axis2{});

    // The trigger is let go and pulled again: it passes while the rest stay latched.
    InputFrame next = held;
    next.right.trigger = 0.0f;
    latch.update(next, false);
    next.right.trigger = 1.0f;
    out = latch.update(next, false);
    CHECK(out.frame.right.trigger == 1.0f);
    CHECK(out.frame.right.grip == 0.0f);
    CHECK_FALSE(out.frame.left.secondaryButton);
    CHECK(out.frame.right.stick == Axis2{});
    CHECK(latch.anyLatched());

    // A button not held through the close presses at once alongside them.
    next.left.primaryButton = true;
    CHECK(latch.update(next, false).frame.left.primaryButton);
}

TEST_CASE("an analog input is let go below its release threshold; a lost value keeps the latch") {
    MenuReleaseLatch latch;
    InputFrame frame = restingFrame();
    frame.right.trigger = 0.6f;
    latch.update(frame, true);
    frame.right.trigger = 0.4f; // above the trigger's release threshold (0.35): still held
    CHECK(latch.update(frame, false).frame.right.trigger == 0.0f);
    frame.right.trigger = std::numeric_limits<float>::quiet_NaN();
    CHECK(latch.update(frame, false).frame.right.trigger == 0.0f);
    frame.right.trigger = 0.6f;
    CHECK(latch.update(frame, false).frame.right.trigger == 0.0f);
    frame.right.trigger = 0.3f;
    CHECK(latch.update(frame, false).frame.right.trigger == doctest::Approx(0.3f));
    frame.right.trigger = 0.6f;
    CHECK(latch.update(frame, false).frame.right.trigger == doctest::Approx(0.6f));
}

TEST_CASE("a new hold latches afresh") {
    MenuReleaseLatch latch;
    latch.update(withLeftSecondary(true), true);
    latch.update(withLeftSecondary(true), false);
    // A second menu opens while B is still latched, and B is let go in it.
    latch.update(withLeftSecondary(false), true);
    CHECK_FALSE(latch.anyLatched());
    CHECK(latch.update(withLeftSecondary(true), false).frame.left.secondaryButton);
}
