#pragma once

// Scripted controller input for rig tests (ETERNALVR_TEST_INPUT=<file>, docs/VR_CONTROLLERS.md).
//
// Without a headset the controllers can still be driven: the layer re-reads a small text file whenever
// it changes and lays its values over what the runtime reports, so a test script (or a person editing
// the file) can press the trigger, push a stick or point a hand, and the whole path from the snapshot
// to the game runs as it would with real controllers.
//
//   # one "key = value" per line; a line not given leaves the runtime's value
//   right.trigger = 1            analog 0..1 (also grip, and grip_curl: the fingers round the grip, apart
//                                from the grip's press, as the Index's squeeze value)
//   left.stick = 0, 1            x, y in -1..1 (also right.stick)
//   right.primary = 1            buttons: primary, secondary, face3, face4, shoulder, click (stick click),
//                                menu; 1 or 0
//   left.thumbrest = 1           touch sensors: thumbrest, primary_touch, secondary_touch, thumbstick_touch,
//                                trackpad_touch, trigger_touch; 1 or 0
//   right.aim = 20, -10          the hand points 20 degrees left and 10 degrees down of LOCAL's -Z
//   left.aim = 0, 0, 90          an optional roll about the pointing axis, counter-clockwise as the user
//                                sees it (the controller's top to the left): 90 turns the left palm up
//   right.position = 0.2, -0.35, -0.3   metres from the head, LOCAL axes (default: the side's rest position)
//   right.sweep = 30, 0.25, 8    the aim's yaw swept by +-30 degrees, sinusoidally with a 0.25 s period, for
//   8
//                                periods from the moment the file first had this line, then still: a smooth
//                                motion at up to amplitude x 2 pi / period degrees per second (754 here) that
//                                stops at full speed (rig tests of the arms against a moving weapon hand)
//   right.velocity = 0, 0, -3    metres per second, LOCAL axes (default: still), for punches and gestures;
//                                the pose does not move with it
//   left.noise = 0.3, 0.05       tracking noise on the hand's pose at the camera hook, new every frame:
//                                millimetres on each axis and degrees about each axis (standard deviations,
//                                Gaussian), for measuring what the aim makes of jitter (testHandNoise)
//
// A hand given an aim is tracked, at its position from the head (or its side's rest position below and
// ahead of the head), with the grip at the same pose. There is no file IO here.

#include "common/pose.hpp"
#include "features/input/axis2.hpp"
#include "features/input/controller_state.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::input {

// right.sweep: amplitude (degrees), period (seconds), periods.
struct TestSweep {
    float amplitudeDegrees = 0.0f;
    float periodSeconds = 1.0f;
    float periods = 1.0f;
    friend bool operator==(const TestSweep&, const TestSweep&) = default;
};

struct TestHand {
    std::optional<float> trigger;
    std::optional<float> grip;
    std::optional<float> gripCurl;
    std::optional<Axis2> stick;
    std::optional<bool> stickClick;
    std::optional<bool> primary;
    std::optional<bool> secondary;
    std::optional<bool> face3;
    std::optional<bool> face4;
    std::optional<bool> shoulder;
    std::optional<bool> menu;
    std::optional<bool> thumbRest;
    std::optional<bool> primaryTouch;
    std::optional<bool> secondaryTouch;
    std::optional<bool> stickTouch;
    std::optional<bool> trackpadTouch;
    std::optional<bool> triggerTouch;
    std::optional<float> aimYawDegrees;   // counter-clockwise seen from above
    std::optional<float> aimPitchDegrees; // positive up
    float aimRollDegrees = 0.0f;          // about the pointing axis, counter-clockwise as the user sees it
    std::optional<Vec3> position;         // from the head, LOCAL axes, metres
    std::optional<Vec3> velocity;         // LOCAL axes, metres per second
    std::optional<TestSweep> sweep;
    double sweepStartSeconds = 0.0; // when the file first had this sweep (the caller's clock)
    float noiseMillimetres = 0.0f;  // the pose's noise (testHandNoise): per axis, standard deviation
    float noiseDegrees = 0.0f;
};

struct TestInput {
    std::array<TestHand, 2> hands; // indexed by Hand
    std::vector<std::string> issues;

    [[nodiscard]] const TestHand& hand(Hand which) const { return hands[static_cast<std::size_t>(which)]; }
};

TestInput parseTestInput(std::string_view text);

// The pose a hand's aim gives, placed relative to the head at `headPosition` (LOCAL), at `nowSeconds` on the
// clock of TestHand::sweepStartSeconds (a sweep's yaw); nullopt when the file gives it no aim.
std::optional<Pose>
testHandPose(const TestInput& input, Hand hand, Vec3 headPosition, double nowSeconds = 0.0);

// A newly read file's sweeps start at `nowSeconds`, but a sweep the last file had too keeps its start (the
// file is rewritten for other lines, a capture's buttons, while a sweep runs).
void startSweeps(TestInput& input, const TestInput* last, double nowSeconds);

// True when the file gives `hand` an aim, so its poses are the file's and not the runtime's.
bool scriptPosesHand(const TestInput& input, Hand hand);

// The hand's pose with the file's noise for frame `n` (the same `n` and hand give the same noise): each
// axis's position moved by a Gaussian of noiseMillimetres, and the orientation turned about each axis by one
// of noiseDegrees; the pose unchanged with no noise.
Pose testHandNoise(const TestInput& input, Hand hand, Pose pose, std::uint64_t n);

// Lays the file's values over a frame: buttons, analogs and sticks, and the aim pose of each hand given one
// (relative to the frame's head, or LOCAL's origin when the head is not tracked).
void applyTestInput(const TestInput& input, InputFrame& frame);

} // namespace evr::input
