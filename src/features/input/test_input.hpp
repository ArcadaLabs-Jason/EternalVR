#pragma once

// Scripted controller input for rig tests (ETERNALVR_TEST_INPUT=<file>, docs/VR_CONTROLLERS.md).
//
// Without a headset the controllers can still be driven: the layer re-reads a small text file whenever
// it changes and lays its values over what the runtime reports, so a test script (or a person editing
// the file) can press the trigger, push a stick or point a hand, and the whole path from the snapshot
// to the game runs as it would with real controllers.
//
//   # one "key = value" per line; a line not given leaves the runtime's value
//   right.trigger = 1            analog 0..1 (also grip)
//   left.stick = 0, 1            x, y in -1..1 (also right.stick)
//   right.primary = 1            buttons: primary, secondary, click (stick click), menu; 1 or 0
//   right.aim = 20, -10          the hand points 20 degrees left and 10 degrees down of LOCAL's -Z
//   right.position = 0.2, -0.35, -0.3   metres from the head, LOCAL axes (default: the side's rest position)
//
// A hand given an aim is tracked, at its position from the head (or its side's rest position below and
// ahead of the head), with the grip at the same pose. There is no file IO here.

#include "common/pose.hpp"
#include "features/input/axis2.hpp"
#include "features/input/controller_state.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::input {

struct TestHand {
    std::optional<float> trigger;
    std::optional<float> grip;
    std::optional<Axis2> stick;
    std::optional<bool> stickClick;
    std::optional<bool> primary;
    std::optional<bool> secondary;
    std::optional<bool> menu;
    std::optional<float> aimYawDegrees;   // counter-clockwise seen from above
    std::optional<float> aimPitchDegrees; // positive up
    std::optional<Vec3> position;         // from the head, LOCAL axes, metres
};

struct TestInput {
    std::array<TestHand, 2> hands; // indexed by Hand
    std::vector<std::string> issues;

    [[nodiscard]] const TestHand& hand(Hand which) const { return hands[static_cast<std::size_t>(which)]; }
};

TestInput parseTestInput(std::string_view text);

// The pose a hand's aim gives, placed relative to the head at `headPosition` (LOCAL); nullopt when the file
// gives it no aim.
std::optional<Pose> testHandPose(const TestInput& input, Hand hand, Vec3 headPosition);

// Lays the file's values over a frame: buttons, analogs and sticks, and the aim pose of each hand given one
// (relative to the frame's head, or LOCAL's origin when the head is not tracked).
void applyTestInput(const TestInput& input, InputFrame& frame);

} // namespace evr::input
