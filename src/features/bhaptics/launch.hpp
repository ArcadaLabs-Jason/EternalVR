#pragma once

// Launched by a jump pad or a booster (a player's idea, public issue #1: the force from the legs, as for a
// landing). The layer's hooks see the game launch the player (bhaptics_launch.cpp): a jump pad
// (idTrigger_BouncePad) or a booster that blasts the player to a destination (idTrigger_SonicBoom). A pad's
// trigger may run again on the next frames while the player is still in its volume, so LaunchFilter takes
// touches less than kLaunchRepeatSeconds apart for one launch. Jumps, double jumps, dashes and landings do
// not go through those paths at all. Pure.

#include <array>
#include <cstddef>

namespace evr::bhaptics {

// What launched the player.
enum class LaunchSource : unsigned char { JumpPad, Booster };

const char* launchSourceName(LaunchSource source);

// A touch this soon after the previous one (of any pad or booster) belongs to the same launch. A pad's flight
// lasts a second or more, so the next pad of a chain is a new launch.
inline constexpr double kLaunchRepeatSeconds = 0.5;

class LaunchFilter {
public:
    // A touch of a pad or a booster at `seconds` (a steady clock): true when it starts a new launch. Every
    // touch, new or not, moves the window on. A time that is not finite is ignored.
    bool note(double seconds);

    void reset();

private:
    bool seen_ = false;
    double lastTouch_ = 0.0;
};

// The launch's shove on the vest, front and back: the bottom row from strong to weak, then the row above
// it joining in, a step every kLaunchStepSeconds, each frame lasting kLaunchMillis (a little longer than a
// step, so the curve does not stutter). About 0.22 s in all.
struct LaunchStep {
    float bottom = 0.0f; // the bottom row
    float above = 0.0f;  // the row above it
};

inline constexpr std::array<LaunchStep, 4> kLaunchSteps{
    {{85.0f, 0.0f}, {65.0f, 40.0f}, {45.0f, 30.0f}, {30.0f, 20.0f}}};
inline constexpr double kLaunchStepSeconds = 0.05;
inline constexpr double kLaunchSeconds = kLaunchStepSeconds * static_cast<double>(kLaunchSteps.size());
inline constexpr int kLaunchMillis = 70;

// The step under way `sinceStart` seconds into a launch's curve, or nullptr outside it.
const LaunchStep* launchStepAt(double sinceStart);

} // namespace evr::bhaptics
