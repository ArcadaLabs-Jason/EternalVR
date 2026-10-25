// Motion controllers, the touches on controls bound like buttons (controllers.hpp,
// features/input/touch_inputs.hpp, DECISIONS T-126): their log lines, as the thumb rest bound like a button
// has its own (rest_wheel.cpp).
//
// The first touch each bound sensor reports is logged, and one line says so when none has after a minute of
// play: a runtime may not report a sensor (an emulation that leaves it out). Every 10 s in which any count
// moved, one line per bound sensor counts what it did: touches, those shorter than the debounce, gaps
// forgiven, taps, holds, touches ended by their own control in use, brushes (too short to tap) and landings
// voided straight after their own control, so a player's log shows whether a binding fires by mistake or not
// at all. Nothing is logged while no map binds a touch on a control. Under ETERNALVR_CONTROLLERS_TRACE every
// change of the touch sensors the frame reports is logged whether bound or not (the first 200, then one a
// minute), so a scripted touch can be seen reaching the layer.

#include "vkcore/controllers_impl.hpp"
#include "vkcore/controllers_input_logs.hpp"

#include "features/input/binding_keys.hpp"
#include "features/input/touch_inputs.hpp"
#include "vkcore/log.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";
constexpr float kNoTouchNoteSeconds = 60.0f;
constexpr float kStatsSeconds = 10.0f;

using PerTouch = std::array<std::array<bool, input::kControlTouchCount>, 2>;

// Mapper only (mapperMutex).
PerTouch g_seen{};
float g_untouchedSeconds = 0.0f;
bool g_noTouchLogged = false;
std::array<std::array<input::TouchButtonStats, input::kControlTouchCount>, 2> g_logged{};
float g_statsSeconds = 0.0f;
LogCap g_lines{360}; // up to an hour of 10 s lines, then one a minute
std::array<std::array<bool, input::kControlTouchCount + 1>, 2> g_traced{}; // the thumb rest last
LogCap g_traceLines{200};

const char* side(input::Hand hand) {
    return hand == input::Hand::Left ? "left" : "right";
}

// "primary_touch": the input's name in a control map key.
std::string touchName(input::ButtonInput touch) {
    const std::string key = input::formatBindingKey(input::ButtonKey{input::Hand::Left, touch});
    const std::size_t first = key.find('.') + 1;
    return key.substr(first, key.rfind('.') - first);
}

// The touches the map binds, per hand.
PerTouch boundTouches(const input::InputMapper& mapper) {
    PerTouch bound{};
    for (const input::Hand hand : {input::Hand::Left, input::Hand::Right}) {
        for (std::size_t t = 0; t < input::kControlTouchCount; ++t) {
            bound[static_cast<std::size_t>(hand)][t] =
                input::bindsInput(mapper.profile(), hand, input::kControlTouches[t]);
        }
    }
    return bound;
}

bool any(const PerTouch& touches) {
    for (const auto& hand : touches) {
        for (const bool t : hand) {
            if (t) {
                return true;
            }
        }
    }
    return false;
}

// Under the trace: the touch sensors the frame reports, when they change ("right primary_touch", or "none").
void traceTouchSensors(const input::InputFrame& frame) {
    std::array<std::array<bool, input::kControlTouchCount + 1>, 2> now{};
    std::string text;
    for (const input::Hand hand : {input::Hand::Left, input::Hand::Right}) {
        const std::size_t h = static_cast<std::size_t>(hand);
        for (std::size_t t = 0; t <= input::kControlTouchCount; ++t) {
            const input::ButtonInput touch =
                t < input::kControlTouchCount ? input::kControlTouches[t] : input::ButtonInput::ThumbRest;
            now[h][t] = input::touchSensed(frame.hand(hand), touch);
            if (now[h][t]) {
                text += text.empty() ? "" : ", ";
                text += std::string(side(hand)) + " " + touchName(touch);
            }
        }
    }
    std::uint64_t skipped = 0;
    if (now != g_traced && g_traceLines.due(GetTickCount64(), skipped)) {
        EVR_LOG("%s: trace: touch sensors: %s", kTag, text.empty() ? "none" : text.c_str());
    }
    g_traced = now;
}

void noteFirstTouches(const input::InputFrame& frame, const PerTouch& bound, float dt) {
    for (const input::Hand hand : {input::Hand::Left, input::Hand::Right}) {
        const std::size_t h = static_cast<std::size_t>(hand);
        for (std::size_t t = 0; t < input::kControlTouchCount; ++t) {
            const input::ButtonInput touch = input::kControlTouches[t];
            if (bound[h][t] && !g_seen[h][t] && input::touchSensed(frame.hand(hand), touch)) {
                g_seen[h][t] = true;
                EVR_LOG("%s: touch button: the %s controller reports %s", kTag, side(hand),
                        touchName(touch).c_str());
            }
        }
    }
    if (any(g_seen) || g_noTouchLogged) {
        return;
    }
    g_untouchedSeconds += dt;
    if (g_untouchedSeconds >= kNoTouchNoteSeconds) {
        g_noTouchLogged = true;
        EVR_LOG("%s: touch button: no touch reported on a bound touch sensor in %.0f s of play; the runtime "
                "may not report them (a touch bound as a button then never presses)",
                kTag, kNoTouchNoteSeconds);
    }
}

void noteStats(const input::InputMapper& mapper, const PerTouch& bound) {
    for (const input::Hand hand : {input::Hand::Left, input::Hand::Right}) {
        const std::size_t h = static_cast<std::size_t>(hand);
        for (std::size_t t = 0; t < input::kControlTouchCount; ++t) {
            const input::ButtonInput touch = input::kControlTouches[t];
            const input::TouchButtonStats& now = mapper.touchButton(hand, touch).stats();
            const input::TouchButtonStats& was = g_logged[h][t];
            const unsigned touches = now.touches - was.touches;
            std::uint64_t skipped = 0;
            if (bound[h][t] && input::statsChanged(now, was) && g_lines.due(GetTickCount64(), skipped)) {
                char note[48] = "";
                if (skipped != 0) {
                    std::snprintf(note, sizeof(note), " (%llu more not logged)",
                                  static_cast<unsigned long long>(skipped));
                }
                EVR_LOG(
                    "%s: %s %s button, last %.0f s: %u touch(es), %u short, %u gap(s) forgiven, %u tap(s), "
                    "%u hold(s), %u ended by its own control, %u brush(es): no tap, %u landing(s) voided "
                    "(own control)%s",
                    kTag, side(hand), touchName(touch).c_str(), kStatsSeconds, touches,
                    now.shortTouches - was.shortTouches, now.gapsForgiven - was.gapsForgiven,
                    now.taps - was.taps, now.holds - was.holds, now.ownUse - was.ownUse,
                    now.brushes - was.brushes, now.voided - was.voided, note);
            }
            g_logged[h][t] = now;
        }
    }
}

} // namespace

void noteTouchButtons(const input::InputFrame& frame, const input::InputMapper& mapper, float dt) {
    if (settings().trace) {
        traceTouchSensors(frame);
    }
    const PerTouch bound = boundTouches(mapper);
    if (!any(bound)) {
        return;
    }
    noteFirstTouches(frame, bound, dt);
    g_statsSeconds += dt;
    if (g_statsSeconds < kStatsSeconds) {
        return;
    }
    g_statsSeconds = 0.0f;
    noteStats(mapper, bound);
}

void restartTouchButtonStats() {
    g_logged = {};
}

} // namespace evr::vkcore::controllers
