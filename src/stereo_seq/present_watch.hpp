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

private:
    double stopAfter_;
    bool started_ = false;
    std::uint64_t presents_ = 0;
    double moved_ = 0.0; // when the count was last seen to move
    bool stopped_ = false;
    double quiet_ = 0.0;
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
