#pragma once

// The controllers' thumb-rest wheel on the game's side and the input log lines (rest_wheel.cpp,
// touch_buttons_log.cpp, input_summary_log.cpp), split from controllers_impl.hpp.

#include "features/input/controller_state.hpp"
#include "features/input/game_input.hpp"
#include "features/input/input_mapper.hpp"
#include "vkcore/controllers_impl.hpp"

namespace evr::vkcore::controllers {

// The thumb-rest weapon wheel (rest_wheel.cpp, features/input/rest_wheel.hpp). The mapper, under mapperMutex:
// whether the game has it, and the reach and the wrist flick, start nothing this frame (the
// game suppresses buttons, forces the view, plays a cutscene the player may skip, or a demon is piloted); and
// after the mapper ran, its log lines and whether the rests report a touch at all.
bool restWheelBlocked(const State& s);
void noteRestWheel(const input::InputFrame& frame, const input::GameInput& input, float dt);
// ensureMapper, under mapperMutex, once it built a new mapper: its wheel counts from zero again, and so do
// the 10 s touch lines (their clock runs on).
void restartRestWheelStats();
// The touches on controls bound like buttons (touch_buttons_log.cpp, features/input/touch_inputs.hpp). The
// mapper, under mapperMutex, after it ran (from noteRestWheel): their log lines; and from
// restartRestWheelStats, their counts from zero again.
void noteTouchButtons(const input::InputFrame& frame, const input::InputMapper& mapper, float dt);
void restartTouchButtonStats();
// The 10 s input line (input_summary_log.cpp, features/input/input_summary.hpp). The mapper, under
// mapperMutex, once a frame after the menu hold: `asked` is the mapper's output before a menu held it back,
// `sent` what went to the game; and from restartRestWheelStats, the window from zero again.
// runMapper holds the frame for the 10 s input line (mapperMutex held); the command hook adds it with whether
// its write reached the game (`sent` counts what was written, not what was asked), or the pad hook with true.
void stageInputSummary(const input::InputFrame& frame,
                       const input::GameInput& asked,
                       const MappedInput& sent,
                       float dt,
                       bool menuHold);
void commitInputSummary(bool reachedGame);
// The input went stale: held actions count as new presses afterwards, the window keeps its time.
void gapInputSummary();
void restartInputSummary();
// Adds the staged frame when the scope ends, whatever way the hook leaves it.
struct InputSummaryCommit {
    bool reachedGame = false;
    ~InputSummaryCommit() { commitInputSummary(reachedGame); }
};
// installGameHooks, with the thumb-rest wheel on: finds the game's wheel open delay
// (weaponWheel_HoldTimeForOpeningWheel) and, when the player turned the wheel's slowdown off,
// weaponWheel_slowTimeScale (each logged when not found).
void locateWheelCvars();
// The mapper: the least time the thumb-rest wheel holds the game's wheel, from the game's open delay now
// (input::wheelHoldSeconds; logged when the delay changes).
float restWheelHoldSeconds();

} // namespace evr::vkcore::controllers
