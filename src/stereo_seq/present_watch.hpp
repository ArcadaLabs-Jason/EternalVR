#pragma once

// The game's presents as the layer sees them (docs/VR_STEREO.md, Desktop window): whether the game has
// stopped presenting while the headset runs (its render thread is stuck: the headset then repeats the last
// image), and the test knob that makes one present report VK_ERROR_OUT_OF_DATE_KHR. Plain logic, tested on
// every platform.

#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::stereo_seq {

// Fed the game's present count by the XR worker at a steady interval. Stopped once the count has not moved
// for `stopAfter` seconds (once, until it moves again); Resumed at the first check that sees it move after
// that. The first check only starts the clock.
class PresentWatch {
public:
    enum class Event : std::uint8_t { None, Stopped, Resumed };
    explicit PresentWatch(double stopAfter) : stopAfter_(stopAfter) {}

    // `presents`: the game's presents so far; `seconds`: a monotonic clock.
    Event check(std::uint64_t presents, double seconds);
    // Seconds since the count last moved: at Stopped, how long it has not; at Resumed, how long it had not
    // (to the check that saw it move).
    [[nodiscard]] double quiet() const { return quiet_; }
    // From Stopped until the count moves again.
    [[nodiscard]] bool stopped() const { return stopped_; }
    // When the count was last seen to move (the clock of `check`).
    [[nodiscard]] double lastMoved() const { return moved_; }

private:
    double stopAfter_;
    bool started_ = false;
    std::uint64_t presents_ = 0;
    double moved_ = 0.0; // when the count was last seen to move
    bool stopped_ = false;
    double quiet_ = 0.0;
};

// The newest failed present result, as the stop's report sees it. OutOfDate: VK_ERROR_OUT_OF_DATE_KHR or
// VK_ERROR_SURFACE_LOST_KHR (the game was to recreate its swapchain).
enum class PresentFailure : std::uint8_t { None, OutOfDate, Other };

// The Vulkan results behind OutOfDate (checked against the Vulkan headers where they are used).
constexpr int kResultOutOfDate = -1000001004;   // VK_ERROR_OUT_OF_DATE_KHR
constexpr int kResultSurfaceLost = -1000000000; // VK_ERROR_SURFACE_LOST_KHR

// The newest failed present before a stop.
struct StopCause {
    PresentFailure failure = PresentFailure::None;
    bool lastPresent = false; // it was the game's last present (no present came after it)
};

// `result`: the newest failed present result (0: none yet); `resultAt`: the game's present count at it;
// `presents`: the count now.
StopCause stopCause(int result, std::uint64_t resultAt, std::uint64_t presents);

// A stop whose last present came this long or less after the watch's first check is at the game's start.
constexpr double kStopAtStartSeconds = 120.0;
// `lastMoved`: PresentWatch::lastMoved; `firstCheck`: the watch's first check (the same clock).
bool stopAtStart(double lastMoved, double firstCheck);

// The game's window and the desktop at a check. A stop while the window is minimised, or while the input is
// on another desktop than the user's own (the lock screen, a UAC prompt: the secure desktop), is the game
// waiting for its window, not stuck.
struct StopScene {
    bool minimised = false;
    bool otherDesktop = false;
};

// The game's window is shown on the user's desktop.
bool shown(StopScene scene);

// At a stop, the layer's one report (the status file asking the player to restart the game, and the window
// told once to read its client area again, which is not known to free a stuck game): only when the game's
// last present before the stop failed out of date or with a lost surface, and only once per process. Never
// while the window is minimised or the input is on another desktop, nor at a check right after that (a window
// restored just before a check has not had the time to present again); a stop that lasts past that is
// reported then.
class StopRecovery {
public:
    // Every check: `stopped`: PresentWatch::stopped after this check; `cause`: the newest failed present;
    // `scene`: the window and the desktop now.
    bool due(bool stopped, StopCause cause, StopScene scene);

private:
    bool done_ = false;
    bool shownBefore_ = false; // the previous check saw the window shown
};

// ETERNALVR_TEST_PRESENT_OUT_OF_DATE=<seconds>: a positive number of seconds, up to a day; nullopt for
// anything else (unset, 0 and text included).
std::optional<double> parseTestSeconds(std::wstring_view text);

// The test knob's one present: due once, at the first present `after` seconds or more past the first one it
// saw (`seconds`: a monotonic clock).
class OneShotAfter {
public:
    explicit OneShotAfter(double after) : after_(after) {}
    bool due(double seconds);

private:
    double after_;
    bool started_ = false;
    double first_ = 0.0;
    bool done_ = false;
};

} // namespace evr::stereo_seq
