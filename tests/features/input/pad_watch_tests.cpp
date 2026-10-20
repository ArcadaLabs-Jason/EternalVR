#include "features/input/pad_watch.hpp"

#include "features/input/virtual_gamepad.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

using evr::input::kLeftStickDeadzone;
using evr::input::kPadStaleSeconds;
using evr::input::kRightStickDeadzone;
using evr::input::PadMotion;
using evr::input::PadState;
using evr::input::PadWatch;
using evr::input::stickDeflection;
namespace pad_button = evr::input::pad_button;

namespace {

// XInput states as the virtual Xbox pad the rig drives (ViGEmBus) reports them: full deflection is 32767.
constexpr std::int16_t kFull = 32767;

PadState buttons(std::uint16_t bits) {
    PadState p;
    p.buttons = bits;
    return p;
}

PadState sticks(std::int16_t lx, std::int16_t ly, std::int16_t rx, std::int16_t ry) {
    PadState p;
    p.thumbLX = lx;
    p.thumbLY = ly;
    p.thumbRX = rx;
    p.thumbRY = ry;
    return p;
}

constexpr std::uint16_t kBoth = pad_button::kLeftThumb | pad_button::kRightThumb;

} // namespace

TEST_CASE("a stick's deflection counts from XInput's deadzone to the rim") {
    CHECK(stickDeflection(0, 0, kLeftStickDeadzone) == 0.0f);
    CHECK(stickDeflection(kLeftStickDeadzone, 0, kLeftStickDeadzone) == 0.0f);
    CHECK(stickDeflection(0, kFull, kLeftStickDeadzone) == doctest::Approx(1.0f));
    CHECK(stickDeflection(-32768, 0, kLeftStickDeadzone) == doctest::Approx(1.0f)); // clamped to the rim
    const int half = kLeftStickDeadzone + (kFull - kLeftStickDeadzone) / 2;
    CHECK(stickDeflection(0, -half, kLeftStickDeadzone) == doctest::Approx(0.5f).epsilon(0.001));
    // A diagonal at the corner of the square is the rim, not more.
    CHECK(stickDeflection(kFull, kFull, kRightStickDeadzone) == doctest::Approx(1.0f));
}

TEST_CASE("both sticks pressed on one pad the game reads is the recenter hold") {
    PadWatch watch;
    CHECK_FALSE(watch.bothSticks(1.0));
    watch.update(0, buttons(kBoth), 1.0);
    CHECK(watch.bothSticks(1.0));
    CHECK(watch.bothSticks(1.0 + kPadStaleSeconds));
    // Either stick alone is a click the game takes (the right one melees), not the hold.
    watch.update(0, buttons(pad_button::kRightThumb), 1.1);
    CHECK_FALSE(watch.bothSticks(1.1));
    watch.update(0, buttons(pad_button::kLeftThumb | pad_button::kA), 1.2);
    CHECK_FALSE(watch.bothSticks(1.2));
    // Other buttons held with the sticks do not matter.
    watch.update(0, buttons(kBoth | pad_button::kRightShoulder), 1.3);
    CHECK(watch.bothSticks(1.3));
}

TEST_CASE("the recenter hold is on whichever pad slot the game reads it from") {
    PadWatch watch;
    watch.update(0, std::nullopt, 2.0);
    watch.update(2, buttons(kBoth), 2.0);
    watch.update(3, std::nullopt, 2.0);
    CHECK(watch.bothSticks(2.0));
    // One stick on each of two pads is not one player holding both.
    PadWatch two;
    two.update(0, buttons(pad_button::kLeftThumb), 2.0);
    two.update(1, buttons(pad_button::kRightThumb), 2.0);
    CHECK_FALSE(two.bothSticks(2.0));
}

TEST_CASE("a pad unplugged or no longer read counts as let go") {
    PadWatch watch;
    watch.update(1, buttons(kBoth), 3.0);
    watch.update(1, std::nullopt, 3.1);
    CHECK_FALSE(watch.bothSticks(3.1));
    watch.update(1, buttons(kBoth), 4.0);
    CHECK_FALSE(watch.bothSticks(4.0 + kPadStaleSeconds + 0.01));
    // Past XInput's four users: ignored.
    PadWatch past;
    past.update(4, buttons(kBoth), 5.0);
    CHECK_FALSE(past.bothSticks(5.0));
}

TEST_CASE("the vignette's motion is the move stick and the look stick's sideways push") {
    PadWatch watch;
    CHECK(watch.motion(1.0).move == 0.0f);
    watch.update(0, sticks(0, kFull, 0, 0), 1.0);
    PadMotion m = watch.motion(1.0);
    CHECK(m.move == doctest::Approx(1.0f));
    CHECK(m.turn == 0.0f);
    watch.update(0, sticks(0, 0, -kFull, 0), 1.1);
    m = watch.motion(1.1);
    CHECK(m.move == 0.0f);
    CHECK(m.turn == doctest::Approx(1.0f));
    // The look stick up or down is the head's under head aim: not a turn.
    watch.update(0, sticks(0, 0, 0, kFull), 1.2);
    CHECK(watch.motion(1.2).turn == 0.0f);
    // Inside the deadzones: nothing.
    watch.update(0, sticks(kLeftStickDeadzone - 1, 0, kRightStickDeadzone - 1, 0), 1.3);
    m = watch.motion(1.3);
    CHECK(m.move == 0.0f);
    CHECK(m.turn == 0.0f);
}

TEST_CASE("the vignette takes the largest push among the pads read, and none from a pad gone quiet") {
    PadWatch watch;
    watch.update(0, sticks(0, 20000, 0, 0), 1.0);
    watch.update(1, sticks(0, kFull, 0, 0), 1.0);
    CHECK(watch.motion(1.0).move == doctest::Approx(1.0f));
    watch.update(1, std::nullopt, 1.05);
    CHECK(watch.motion(1.05).move < 1.0f);
    CHECK(watch.motion(1.05).move > 0.0f);
    CHECK(watch.motion(1.0 + kPadStaleSeconds + 0.01).move == 0.0f);
}
