// Motion controllers, the 10 s input line (controllers.hpp, features/input/input_summary.hpp): what the
// sticks did, how many presses each game action got and how many reached the game, and how long gameplay
// input was held back, so a report that the controllers did nothing can be read from the log. One line per
// 10 s of play in which anything happened, and one a minute while nothing does.

#include "features/input/controller_state.hpp"
#include "features/input/game_input.hpp"
#include "features/input/input_summary.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/controllers_input_logs.hpp"
#include "vkcore/log.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

// Mapper only (mapperMutex).
input::InputSummary g_summary;
input::InputSummarySample g_pending; // the mapper's frame, until the command is written
bool g_hasPending = false;
LogCap g_lines{360}; // up to an hour of 10 s lines, then one a minute

} // namespace

void stageInputSummary(const input::InputFrame& frame,
                       const input::GameInput& asked,
                       const MappedInput& sent,
                       float dt,
                       bool menuHold) {
    input::InputSummarySample& sample = g_pending;
    sample = {};
    sample.dtSeconds = dt;
    sample.leftStick = input::magnitude(frame.left.stick);
    sample.rightStick = input::magnitude(frame.right.stick);
    sample.mapped = asked.down;
    sample.sent = sent.actions;
    sample.moveMapped = input::magnitude(asked.move);
    sample.moveSent = input::magnitude(sent.input.move);
    sample.turnMapped = std::fabs(asked.turnDegrees);
    sample.turnSent = std::fabs(sent.input.turnDegrees);
    sample.menuHold = menuHold;
    sample.gameSuppressing = state().gameSuppressing.load(std::memory_order_relaxed);
    sample.forcedView = forcedView();
    g_hasPending = true;
}

void commitInputSummary(bool reachedGame) {
    std::lock_guard lock(state().mapperMutex);
    if (!g_hasPending) {
        return;
    }
    g_hasPending = false;
    g_pending.reachedGame = reachedGame;
    g_summary.add(g_pending);
    input::InputSummaryWindow window;
    if (!g_summary.take(window)) {
        return;
    }
    // Only windows with something in them count against the cap: the idle line is one a minute anyway.
    std::uint64_t skipped = 0;
    if (input::windowActive(window) && !g_lines.due(GetTickCount64(), skipped)) {
        return;
    }
    char text[1024];
    input::formatInputSummary(window, text, sizeof(text));
    if (skipped != 0) {
        EVR_LOG("%s: %s (%llu more not logged)", kTag, text, static_cast<unsigned long long>(skipped));
    } else {
        EVR_LOG("%s: %s", kTag, text);
    }
}

void gapInputSummary() {
    g_summary.gap();
}

void restartInputSummary() {
    g_hasPending = false;
    g_summary.restart();
}

} // namespace evr::vkcore::controllers
