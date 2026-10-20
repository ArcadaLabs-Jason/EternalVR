// The 10 s input summary (features/input/input_summary.hpp): counting presses and stick use, when a
// window is logged, and the line's text.

#include "features/input/input_summary.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>

using evr::game::actionIndex;
using evr::game::add;
using evr::game::GameAction;
using evr::game::GameActionSet;
using evr::input::formatInputSummary;
using evr::input::InputSummary;
using evr::input::InputSummarySample;
using evr::input::InputSummaryWindow;
using evr::input::windowActive;

namespace {

constexpr float kFrame = 0.01f;

GameActionSet actions(std::initializer_list<GameAction> list) {
    GameActionSet set;
    for (const GameAction a : list) {
        add(set, a);
    }
    return set;
}

InputSummarySample frame(GameActionSet mapped, GameActionSet sent, float dt = kFrame) {
    InputSummarySample s;
    s.dtSeconds = dt;
    s.mapped = mapped;
    s.sent = sent;
    return s;
}

// Idle frames for `seconds` and one more, so that a window of them reaches its 10 s whatever the rounding.
void addIdle(InputSummary& summary, float seconds) {
    const int frames = static_cast<int>(seconds / kFrame) + 1;
    for (int i = 0; i < frames; ++i) {
        summary.add(frame({}, {}));
    }
}

std::string line(const InputSummaryWindow& w) {
    char text[1024];
    const std::size_t n = formatInputSummary(w, text, sizeof(text));
    CHECK(n == std::strlen(text));
    return text;
}

} // namespace

TEST_CASE("a press counts when the action goes down, not while it is held") {
    InputSummary summary;
    const GameActionSet fire = actions({GameAction::Fire});
    for (int i = 0; i < 30; ++i) {
        summary.add(frame(fire, fire)); // held 0.3 s: one press
    }
    summary.add(frame({}, {}));
    summary.add(frame(fire, fire)); // a second press
    const InputSummaryWindow& w = summary.window();
    CHECK(w.mappedPresses[actionIndex(GameAction::Fire)] == 2);
    CHECK(w.sentPresses[actionIndex(GameAction::Fire)] == 2);
    CHECK(w.mappedPresses[actionIndex(GameAction::Jump)] == 0);
}

TEST_CASE("presses the controls made that never reached the game are told apart from those that did") {
    InputSummary summary;
    // A menu holds the jump back for a frame, then the game gets a dash the controls also made.
    summary.add(frame(actions({GameAction::Jump}), {}));
    summary.add(frame({}, {}));
    summary.add(frame(actions({GameAction::Dash}), actions({GameAction::Dash})));
    const InputSummaryWindow& w = summary.window();
    CHECK(w.mappedPresses[actionIndex(GameAction::Jump)] == 1);
    CHECK(w.sentPresses[actionIndex(GameAction::Jump)] == 0);
    CHECK(w.mappedPresses[actionIndex(GameAction::Dash)] == 1);
    CHECK(w.sentPresses[actionIndex(GameAction::Dash)] == 1);
}

TEST_CASE("an action that reaches the game later than the controls made it counts as its own send") {
    InputSummary summary;
    const GameActionSet melee = actions({GameAction::Melee});
    summary.add(frame(melee, {}));
    summary.add(frame(melee, melee)); // a hold released it to the game a frame later
    const InputSummaryWindow& w = summary.window();
    CHECK(w.mappedPresses[actionIndex(GameAction::Melee)] == 1);
    CHECK(w.sentPresses[actionIndex(GameAction::Melee)] == 1);
}

TEST_CASE("a frame the layer did not write to the game counts as mapped and as nothing sent") {
    InputSummary summary;
    const GameActionSet fire = actions({GameAction::Fire});
    // A cutscene or a popup: the controls pressed fire and pushed the stick, the command was left alone.
    InputSummarySample suppressed = frame(fire, fire);
    suppressed.moveMapped = 0.9f;
    suppressed.moveSent = 0.9f;
    suppressed.turnMapped = 4.0f;
    suppressed.turnSent = 4.0f;
    suppressed.reachedGame = false;
    summary.add(suppressed);
    const InputSummaryWindow& w = summary.window();
    CHECK(w.mappedPresses[actionIndex(GameAction::Fire)] == 1);
    CHECK(w.sentPresses[actionIndex(GameAction::Fire)] == 0);
    CHECK(w.moveMappedMax == doctest::Approx(0.9f));
    CHECK(w.moveSentMax == 0.0f);
    CHECK(w.turnMappedDegrees == doctest::Approx(4.0f));
    CHECK(w.turnSentDegrees == 0.0f);
    // The popup closes with fire still held: it reaches the game now, and counts as a send then.
    summary.add(frame(fire, fire));
    CHECK(w.sentPresses[actionIndex(GameAction::Fire)] == 1);
    CHECK(w.mappedPresses[actionIndex(GameAction::Fire)] == 1);
}

TEST_CASE("a gap in the input makes a held action a new press and keeps the window's time") {
    InputSummary summary;
    const GameActionSet fire = actions({GameAction::Fire});
    summary.add(frame(fire, fire));
    summary.add(frame({}, {}));
    summary.add(frame(fire, fire)); // held into the gap
    summary.gap();                  // released during it, pressed after
    summary.add(frame(fire, fire));
    const InputSummaryWindow& w = summary.window();
    CHECK(w.mappedPresses[actionIndex(GameAction::Fire)] == 3);
    CHECK(w.sentPresses[actionIndex(GameAction::Fire)] == 3);
    CHECK(w.seconds == doctest::Approx(4 * kFrame));
}

TEST_CASE("a frame's time counts at most 0.1 s") {
    InputSummary summary;
    summary.add(frame({}, {}, 5000.0f));
    CHECK(summary.window().seconds == doctest::Approx(0.1f));
}

TEST_CASE("a stick's largest deflection and the time it was off centre") {
    InputSummary summary;
    for (int i = 0; i < 100; ++i) { // 1 s out at 0.6
        InputSummarySample s = frame({}, {});
        s.rightStick = 0.6f;
        summary.add(s);
    }
    for (int i = 0; i < 50; ++i) { // 0.5 s inside the centre
        InputSummarySample s = frame({}, {});
        s.rightStick = 0.2f;
        s.leftStick = 0.9f; // the left one out for the same time
        summary.add(s);
    }
    InputSummarySample peak = frame({}, {});
    peak.rightStick = 1.0f;
    summary.add(peak);
    const InputSummaryWindow& w = summary.window();
    CHECK(w.stickMax[1] == doctest::Approx(1.0f));
    CHECK(w.stickOffSeconds[1] == doctest::Approx(1.01f).epsilon(0.001));
    CHECK(w.stickMax[0] == doctest::Approx(0.9f));
    CHECK(w.stickOffSeconds[0] == doctest::Approx(0.5f).epsilon(0.001));
    CHECK(w.seconds == doctest::Approx(1.51f).epsilon(0.001));
}

TEST_CASE("move and turn are counted as made and as sent") {
    InputSummary summary;
    InputSummarySample s = frame({}, {});
    s.moveMapped = 0.8f;
    s.moveSent = 0.0f; // held back
    s.turnMapped = 3.0f;
    s.turnSent = 1.0f;
    summary.add(s);
    summary.add(s);
    const InputSummaryWindow& w = summary.window();
    CHECK(w.moveMappedMax == doctest::Approx(0.8f));
    CHECK(w.moveSentMax == doctest::Approx(0.0f));
    CHECK(w.turnMappedDegrees == doctest::Approx(6.0f));
    CHECK(w.turnSentDegrees == doctest::Approx(2.0f));
}

TEST_CASE("the seconds input was held back are told by reason") {
    InputSummary summary;
    for (int i = 0; i < 100; ++i) {
        InputSummarySample s = frame({}, {});
        s.menuHold = i < 60;
        s.gameSuppressing = i < 30;
        s.forcedView = i >= 90;
        summary.add(s);
    }
    const InputSummaryWindow& w = summary.window();
    CHECK(w.menuSeconds == doctest::Approx(0.6f).epsilon(0.001));
    CHECK(w.suppressedSeconds == doctest::Approx(0.3f).epsilon(0.001));
    CHECK(w.forcedViewSeconds == doctest::Approx(0.1f).epsilon(0.001));
}

TEST_CASE("values that are not numbers or are negative count as zero") {
    InputSummary summary;
    InputSummarySample s = frame({}, {}, std::numeric_limits<float>::quiet_NaN());
    s.leftStick = std::numeric_limits<float>::infinity();
    s.moveSent = -1.0f;
    s.turnMapped = std::numeric_limits<float>::quiet_NaN();
    summary.add(s);
    s.dtSeconds = -1.0f;
    summary.add(s);
    const InputSummaryWindow& w = summary.window();
    CHECK(w.seconds == 0.0f);
    CHECK(w.stickMax[0] == 0.0f);
    CHECK(w.moveSentMax == 0.0f);
    CHECK(w.turnMappedDegrees == 0.0f);
    CHECK_FALSE(windowActive(w));
}

TEST_CASE("a window is taken after 10 s, and only when something happened in it") {
    InputSummary summary;
    InputSummaryWindow out;
    summary.add(frame(actions({GameAction::Fire}), actions({GameAction::Fire})));
    addIdle(summary, 9.0f);
    CHECK_FALSE(summary.take(out)); // not full yet
    addIdle(summary, 1.0f);
    REQUIRE(summary.take(out));
    CHECK(out.mappedPresses[actionIndex(GameAction::Fire)] == 1);
    CHECK(out.seconds >= 10.0f);
    CHECK(summary.window().seconds == 0.0f); // the next one starts empty
}

TEST_CASE("an idle window is logged once a minute of play has passed since the last line") {
    InputSummary summary;
    InputSummaryWindow out;
    int logged = 0;
    for (int w = 1; w <= 12; ++w) { // 120 s of nothing
        addIdle(summary, 10.0f);
        if (summary.take(out)) {
            ++logged;
            CHECK_FALSE(windowActive(out));
            CHECK((w == 6 || w == 12));
        }
    }
    CHECK(logged == 2);
}

TEST_CASE("an active window resets the idle minute") {
    InputSummary summary;
    InputSummaryWindow out;
    for (int w = 0; w < 5; ++w) {
        addIdle(summary, 10.0f);
        CHECK_FALSE(summary.take(out));
    }
    InputSummarySample s = frame({}, {});
    s.menuHold = true;
    summary.add(s);
    addIdle(summary, 10.0f);
    CHECK(summary.take(out)); // active, logged
    for (int w = 0; w < 5; ++w) {
        addIdle(summary, 10.0f);
        CHECK_FALSE(summary.take(out)); // the idle minute starts again
    }
    addIdle(summary, 10.0f);
    CHECK(summary.take(out));
}

TEST_CASE("a press held across the window's end is not counted again") {
    InputSummary summary;
    InputSummaryWindow out;
    const GameActionSet fire = actions({GameAction::Fire});
    for (int i = 0; i < 1001; ++i) {
        summary.add(frame(fire, fire));
    }
    REQUIRE(summary.take(out));
    CHECK(out.mappedPresses[actionIndex(GameAction::Fire)] == 1);
    summary.add(frame(fire, fire)); // still held in the next window
    CHECK(summary.window().mappedPresses[actionIndex(GameAction::Fire)] == 0);
}

TEST_CASE("after a restart a held action is a new press") {
    InputSummary summary;
    const GameActionSet fire = actions({GameAction::Fire});
    summary.add(frame(fire, fire));
    summary.restart();
    CHECK(summary.window().seconds == 0.0f);
    summary.add(frame(fire, fire));
    CHECK(summary.window().mappedPresses[actionIndex(GameAction::Fire)] == 1);
}

TEST_CASE("the line names each action pressed with its counts, and what held input back") {
    InputSummary summary;
    InputSummaryWindow out;
    for (int i = 0; i < 1000; ++i) {
        InputSummarySample s = frame({}, {});
        s.rightStick = i < 420 ? 0.93f : 0.0f;
        s.moveMapped = s.rightStick;
        s.moveSent = i < 300 ? s.rightStick : 0.0f;
        s.menuHold = i >= 300 && i < 620;
        if (i == 10 || i == 20 || i == 30) {
            s.mapped = actions({GameAction::Fire});
            s.sent = actions({GameAction::Fire});
        }
        if (i == 400) {
            s.mapped = actions({GameAction::Jump});
        }
        summary.add(s);
    }
    summary.add(frame({}, {}));
    REQUIRE(summary.take(out));
    CHECK(line(out) ==
          "input, last 10 s: left stick max 0.00, off centre 0.0 s; right stick max 0.93, off centre 4.2 s; "
          "move max 0.93 (sent 0.93); turn 0 deg (sent 0); presses mapped/sent: fire 3/3, jump 1/0; held "
          "back: menu 3.2 s, game suppressing buttons 0.0 s, forced view 0.0 s");
}

TEST_CASE("an idle line says nothing was pressed and nothing held back") {
    InputSummary summary;
    InputSummaryWindow out;
    for (int i = 0; i < 6; ++i) {
        addIdle(summary, 10.0f);
        summary.take(out);
    }
    CHECK(line(out) ==
          "input, last 10 s: left stick max 0.00, off centre 0.0 s; right stick max 0.00, off centre 0.0 s; "
          "move max 0.00 (sent 0.00); turn 0 deg (sent 0); presses mapped/sent: none; held back: none");
}

TEST_CASE("a line that does not fit is cut, terminated and never written past the buffer") {
    InputSummaryWindow w;
    w.seconds = 10.0f;
    for (std::size_t i = 0; i < evr::game::kGameActionCount; ++i) {
        w.mappedPresses[i] = 99999;
        w.sentPresses[i] = 99999;
    }
    w.menuSeconds = 3.2f;
    char text[1024];
    CHECK(formatInputSummary(w, text, sizeof(text)) < sizeof(text));
    // The held-back text survives and the cut list says so.
    char shortText[400];
    const std::size_t cutLength = formatInputSummary(w, shortText, sizeof(shortText));
    const std::string cut(shortText, cutLength);
    CHECK(cutLength < sizeof(shortText));
    CHECK(cut.find("...; held back: menu 3.2 s, game suppressing buttons 0.0 s, forced view 0.0 s") !=
          std::string::npos);
    CHECK(cut.find("presses mapped/sent: fire 99999/99999") != std::string::npos);
    for (const std::size_t size : {std::size_t{0}, std::size_t{1}, std::size_t{7}, std::size_t{120}}) {
        char guarded[130];
        std::memset(guarded, 'x', sizeof(guarded));
        const std::size_t n = formatInputSummary(w, guarded, size);
        if (size == 0) {
            CHECK(n == 0);
        } else {
            CHECK(n < size);
            CHECK(guarded[n] == '\0');
        }
        for (std::size_t i = size; i < sizeof(guarded); ++i) {
            CHECK(guarded[i] == 'x');
        }
    }
}
