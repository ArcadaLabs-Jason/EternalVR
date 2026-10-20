#pragma once

// The controllers' input over a window of play, for the 10 s "controllers: input" log line (vkcore/
// input_summary_log.cpp, docs/VR_CONTROLLERS.md, Logs): what each hand's stick did, how many presses each
// game action got from the controls and how many of them were sent to the game, and for how long gameplay
// input was held back. A report that "nothing works" can then be read from the log: a stick that never left
// the centre, a control bound to nothing, or input a menu or the game itself held back.
//
// Plain values and no allocation: the mapper's thread adds one sample per frame and takes a window when it
// is full.

#include "features/input/input_mapper.hpp"
#include "game/eternal/game_action.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace evr::input {

// A stick counts as off centre past this (kOwnStickOut, kArmDeadzone: the layer's other "stick in use"
// lines).
inline constexpr float kSummaryStickCentre = 0.25f;
// A window is full with this many seconds of play.
inline constexpr float kSummaryWindowSeconds = 10.0f;
// A window with nothing in it (no press, no stick out, nothing held back) is logged only once this many
// seconds of play have passed since the last line.
inline constexpr float kSummaryIdleSeconds = 60.0f;

// One mapper frame.
struct InputSummarySample {
    float dtSeconds = 0.0f;
    float leftStick = 0.0f; // the raw stick's deflection, 0..1 (magnitude)
    float rightStick = 0.0f;
    game::GameActionSet mapped; // the actions the controls hold, as the mapper reads them
    game::GameActionSet sent;   // the actions held in the game's command, after a menu or the hold
    // The frame's values reached the game's command. False when the layer wrote nothing (the game
    // suppresses buttons, or the command was not touched): the sent actions, move and turn then count as
    // zero, whatever the fields below hold.
    bool reachedGame = true;
    float moveMapped = 0.0f; // the move axis' magnitude as the mapper made it, and as sent
    float moveSent = 0.0f;
    float turnMapped = 0.0f; // the turn in degrees this frame (absolute value), as made and as sent
    float turnSent = 0.0f;
    bool menuHold = false;        // a menu is up (or a control is held from one): the pointer drives it
    bool gameSuppressing = false; // the game suppresses buttons (a popup, a cutscene)
    bool forcedView = false;      // the game forces the view (a Glory Kill, a camera animation)
};

// What one window holds.
struct InputSummaryWindow {
    float seconds = 0.0f;
    std::array<float, 2> stickMax{}; // per hand (Hand index)
    std::array<float, 2> stickOffSeconds{};
    std::array<std::uint32_t, game::kGameActionCount> mappedPresses{};
    std::array<std::uint32_t, game::kGameActionCount> sentPresses{};
    float moveMappedMax = 0.0f;
    float moveSentMax = 0.0f;
    float turnMappedDegrees = 0.0f; // the sum of the turns
    float turnSentDegrees = 0.0f;
    float menuSeconds = 0.0f;
    float suppressedSeconds = 0.0f;
    float forcedViewSeconds = 0.0f;
};

// Whether anything happened in the window: a press, a stick out of the centre, input held back.
bool windowActive(const InputSummaryWindow& window);

class InputSummary {
public:
    // Adds a frame (a non-finite or negative dt, stick or axis counts as zero, a dt over
    // input::kMaxFrameSeconds counts as that; a press counts when the set gains the action since the
    // frame before).
    void add(const InputSummarySample& sample);

    // The input has a gap (stale snapshot): the next frame's held actions count as new presses, the
    // window and its time are kept.
    void gap();

    // When the window holds kSummaryWindowSeconds of play: starts the next one and, if this one is to be
    // logged (something happened in it, or kSummaryIdleSeconds passed since the last line), copies it to
    // `out` and returns true. Presses held across the boundary are not counted twice.
    bool take(InputSummaryWindow& out);

    // The window being filled.
    [[nodiscard]] const InputSummaryWindow& window() const { return window_; }

    // The controllers' input went away or the map was rebuilt: the next frame's held actions count as new
    // presses and the window starts empty.
    void restart();

private:
    InputSummaryWindow window_;
    game::GameActionSet lastMapped_;
    game::GameActionSet lastSent_;
    float quietSeconds_ = 0.0f; // play since the last line
};

// The window as the log line's text after the tag ("input, last 10 s: left stick ..."): writes at most
// `size - 1` characters and a terminator, returns the length.
std::size_t formatInputSummary(const InputSummaryWindow& window, char* out, std::size_t size);

} // namespace evr::input
