#pragma once

// Merging our input into the game's user command (M5, docs/rig-findings/input-aim.md section 1.4).
//
// The layer writes into the command the game has just built from the keyboard, mouse and pad, so real
// input keeps working: buttons are ORed in, movement is added with a clamp, and turning is added to the
// generator's accumulated angles, where the mouse's motion goes too. While the game suppresses buttons
// itself (console open, menus), nothing is ORed in.
//
// The game samples the command at its own rate, which need not match the rate the mapper runs at, so a
// one-frame action (a tap) is held for a minimum time and a minimum number of commands before it is
// released (ActionHold); otherwise a quick switch could fall between two samples.

#include "game/eternal/game_action.hpp"

#include <array>
#include <cstdint>

namespace evr::input {

// The command's move axes hold -128..127; keys give +-127, so injection clamps to that.
inline constexpr int kMaxMoveAxis = 127;

// The command's buttons after injection: `game | injected`, or `game` alone while the game suppresses
// buttons.
std::uint64_t mergeButtons(std::uint64_t game, std::uint64_t injected, bool suppressed);

// One move axis after injection: the game's value plus ours, clamped to +-kMaxMoveAxis. An injected 0
// leaves the game's value exactly as it is (even -128).
std::int8_t addMoveAxis(std::int8_t game, int injected);

// Holds every action that went down for at least `minSeconds` and `minCommands` updates, so a tap is
// seen by the game. An action held longer is released as soon as it is released.
class ActionHold {
public:
    ActionHold(float minSeconds = 0.05f, int minCommands = 2);

    // `down` is this update's actions; `dtSeconds` the time since the previous update (non-finite or
    // negative counts as 0). Returns the actions to send.
    game::GameActionSet update(const game::GameActionSet& down, float dtSeconds);

    void reset();

private:
    struct Held {
        float age = 0.0f;
        int commands = 0;
        bool holding = false;
    };
    float minSeconds_;
    int minCommands_;
    game::GameActionSet previous_;
    std::array<Held, game::kGameActionCount> held_{};
};

// View-angle deltas (degrees) waiting for the next command build: the turn. Positive yaw turns left,
// positive pitch looks down (id Tech).
struct ViewDelta {
    float pitch = 0.0f;
    float yaw = 0.0f;
};

class ViewDeltaQueue {
public:
    // Non-finite values are ignored.
    void add(ViewDelta delta);
    // Everything added since the last drain, then empty.
    ViewDelta drain();
    [[nodiscard]] ViewDelta pending() const { return pending_; }

private:
    ViewDelta pending_;
};

// The generator's accumulated yaw after adding `delta` degrees. The game turns degrees into 16-bit
// angle units (65536 per turn), so whole turns of 360 degrees change nothing; the sum is kept within
// +-3600 degrees so that float precision never degrades over a long session of turning one way.
float addAccumulatedYaw(float accumulated, float delta);

} // namespace evr::input
