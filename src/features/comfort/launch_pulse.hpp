#pragma once

// The comfort vignette's pulse for a launch (vignette.hpp): a jump pad or a booster throws the player (the
// launch hooks, vkcore/bhaptics_launch.cpp), or the Sentinel Hammer's slam throws the player up and drives
// them down (the held hammer's slam start time, SlamWatch below, vkcore/game_motion.cpp). That is motion the
// player sees but does not make with the body, like the stick's. The pulse comes in fast, holds while the
// launch carries the player (until the feet are on the ground again, within a limit sized to the launch) and
// goes out gently; the vignette's look (Light, Strong) stays the player's. Pure: time in seconds on any
// steady clock.

#include <cstdint>

namespace evr::comfort {

struct LaunchPulseTiming {
    float riseSeconds = 0.08f;       // clear to full (a launch is sudden)
    float minHoldSeconds = 0.3f;     // full for at least this long from the launch
    float fallSeconds = 0.6f;        // full to clear
    float leaveGroundSeconds = 0.5f; // still on the ground this long after the launch: it carried no one
};

// What the game says of a launch, for the longest the pulse may hold: a pad's flight time when it gives one
// (seconds), else the launch speed (game units, metres, per second). Non-finite or non-positive: unknown.
struct LaunchSize {
    float flightSeconds = 0.0f;
    float speed = 0.0f;
};

// The longest a launch holds the pulse, landed or not: its flight time and a little more, else the time a
// throw straight up at `speed` takes to come back down under the game's gravity (g_gravity, 20.3 m/s^2),
// within kMinLaunchHoldSeconds..kMaxLaunchHoldSeconds; kDefaultLaunchHoldSeconds when nothing is known.
inline constexpr float kLaunchGravity = 20.3f;
inline constexpr float kMinLaunchHoldSeconds = 0.5f;
inline constexpr float kMaxLaunchHoldSeconds = 4.0f;
inline constexpr float kDefaultLaunchHoldSeconds = 2.0f;
float launchHoldSeconds(const LaunchSize& size);

class LaunchPulse {
public:
    explicit LaunchPulse(LaunchPulseTiming timing = {});

    // A launch at `now` that may hold the pulse for at most `holdCapSeconds` (launchHoldSeconds). One during
    // a pulse carries on from the amount shown (no dip), with its own hold.
    void launch(double now, float holdCapSeconds);
    // Every game frame: whether the player's feet are on the ground. The first time they are after having
    // left it since the launch, the hold ends (not before the minimum); a launch that never lifts the player
    // ends its hold leaveGroundSeconds after it. Ignored without a launch under way, or once it landed.
    void ground(double now, bool onGround);
    // 0 (clear) to 1 (the look's full vignette) at `now`.
    [[nodiscard]] float amount(double now) const;
    // A launch is holding or fading at `now`.
    [[nodiscard]] bool active(double now) const;
    // When the hold of the launch under way ends (or ended): its landing, else its limit, never before the
    // minimum hold.
    [[nodiscard]] double holdEnd() const;
    // The launch under way has landed (ground), and when.
    [[nodiscard]] bool landed() const { return landed_; }
    [[nodiscard]] double landedAt() const { return landedAt_; }
    [[nodiscard]] double start() const { return start_; }
    void reset();

private:
    LaunchPulseTiming timing_;
    bool started_ = false;
    double start_ = 0.0;
    float from_ = 0.0f;    // the amount shown when this launch came
    float holdCap_ = 0.0f; // seconds from start_
    bool leftGround_ = false;
    bool landed_ = false;
    double landedAt_ = 0.0;
};

// One game frame's reading of the Sentinel Hammer: whether it is the hands' held item and, while it is,
// its idHammerWeapon::slamStartTime (game time, any unit). `valid` false when the player could not be read.
struct HammerReading {
    bool valid = false;
    bool held = false;
    std::int64_t slamStartTime = 0;
};

// The Sentinel Hammer's slam: the hammer is in the hands only for a slam (a press with its meter empty
// brings nothing up), and its slam start time takes a new value one game frame after it comes up, as the
// player is thrown up (the test PC: 0.03 s after the press, the landing 0.8 s later). A slam starts when
// the held hammer's start time differs from the last one read off it (kept while it is put away), unless
// it is 0. The first time the hammer is read, and the first after a failed read (a load), its time only
// sets the base, so a hammer that shows without a slam starting (its old time) is never one. The slam
// flags the type info names did not tell it on the rig (`slamInProgress` set only as the slam landed;
// `slamming` a byte that held 0 in one run and 0x4F in another).
class SlamWatch {
public:
    // One game frame's reading. True when a slam started.
    bool update(const HammerReading& r);
    void reset() { *this = SlamWatch(); }

private:
    bool have_ = false;
    std::int64_t last_ = 0;
};

} // namespace evr::comfort
