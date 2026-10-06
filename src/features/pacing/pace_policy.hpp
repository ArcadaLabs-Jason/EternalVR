#pragma once

// Frame pacing (ETERNALVR_PACE, docs/VR_STEREO.md "Frame pacing"). Without it the game renders stereo pairs
// as fast as it can and each headset frame shows the newest finished pair: at 142 pairs a second on a 90 Hz
// headset some headset frames get a pair one game frame newer than the last, others two, so the shown game
// frames are unevenly spaced in time. Head rotation stays smooth (the compositor turns every frame to the
// head) but the world's animation, locomotion and the gun move at an irregular cadence.
//
// With `headset` the game is held to one pair per headset frame: after the game hands a pair to the headset,
// its next frame waits until the headset has begun its next frame (the XR worker's frame, timed by
// xrWaitFrame), so every game frame starts at the same point of the headset's frame loop, as a native VR
// game's does. The wait is bounded (FramePacer::kTimeoutPeriods display periods) and never happens while
// the headset's loop is not running (not shown, lost, stopping), so the game never hangs on it.
//
// FramePacer is the game thread's decision; Cadence counts, per headset frame, how many images were handed
// over since the one before (one each when paced; a mix of one and two when the game runs faster than the
// headset). No OpenXR or Windows here.

#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::pacing {

enum class PaceMode { Off, Headset };

// "off" or "headset", any case, surrounding spaces ignored; nullopt otherwise.
std::optional<PaceMode> parsePaceMode(std::string_view text);
const char* paceModeName(PaceMode mode);

// The headset's frame loop as the XR worker last reported it.
struct HeadsetLoop {
    std::uint64_t frames = 0;      // headset frames begun so far
    double lastFrameSeconds = 0.0; // when the newest one began (a monotonic clock)
    double periodSeconds = 0.0;    // the runtime's display period; 0 while not known
};

// After the game handed an image to the headset: wait or go on.
struct PaceStep {
    bool wait = false;
    std::uint64_t untilFrame = 0; // wait until HeadsetLoop::frames reaches this ...
    double timeoutSeconds = 0.0;  // ... for at most this long
};

class FramePacer {
public:
    // A wait ends after this many display periods without a new headset frame (one frame the runtime
    // skipped still ends it in time); never later than kMaxTimeoutSeconds, whatever the period reads.
    static constexpr double kTimeoutPeriods = 2.0;
    static constexpr double kMaxTimeoutSeconds = 0.05;
    // The headset's loop counts as running while its newest frame began less than this many periods ago;
    // past it the game runs free (a wait never reaches past this point either).
    static constexpr double kIdlePeriods = 3.0;

    struct Counters {
        std::uint64_t handOvers = 0;
        std::uint64_t waits = 0;         // waits that ended with a new headset frame
        std::uint64_t timeouts = 0;      // waits that ended without one (a miss)
        double waitSeconds = 0.0;        // the time spent in both kinds of wait
        double longestWaitSeconds = 0.0; // since the last takeLongestWait
        std::uint64_t frameBegun = 0;    // no wait: a headset frame had begun since the last one (the game is
                                         // at or below the headset's rate)
        std::uint64_t idle = 0;          // no wait: the headset's loop is not running
    };

    explicit FramePacer(PaceMode mode = PaceMode::Off) : mode_(mode) {}

    // The game handed an image to the headset at `nowSeconds` (same clock as the loop's).
    PaceStep afterHandOver(const HeadsetLoop& loop, double nowSeconds);
    // The wait from afterHandOver ended after `waitedSeconds`, the loop at `framesNow` frames; `begun`:
    // the frame it waited for began (else it timed out).
    void waited(std::uint64_t framesNow, double waitedSeconds, bool begun);

    [[nodiscard]] PaceMode mode() const { return mode_; }
    [[nodiscard]] const Counters& counters() const { return counters_; }
    // The longest wait since the last call (the 10 s line's), and starts over.
    double takeLongestWait();

private:
    PaceMode mode_;
    std::uint64_t released_ = 0; // the headset's frame count when the game last went on
    Counters counters_;
};

// While a runtime's menu or dashboard is over the game (the session VISIBLE, or SYNCHRONIZED once hidden,
// after it had focus; public issue #19) the game gets no input, and keep-active stops it from pausing, so it
// would render as fast as it can behind the menu. The cap holds it to one image per display period on its own
// clock, pacing or not, so it holds whether or not the headset's frames come. Not fewer: a session can stay
// VISIBLE while the game is what the player sees (the OpenXR simulator once its window is not in front).
class UnfocusedCap {
public:
    static constexpr double kDefaultPeriodSeconds = 1.0 / 72.0; // while the runtime has given no period
    static constexpr double kMinPeriodSeconds = 1.0 / 90.0;
    static constexpr double kMaxPeriodSeconds = 1.0 / 30.0;
    static constexpr double kPeriodsPerImage = 1.0;

    // The time between images for the runtime's display period (not positive or not finite: none given), the
    // period clamped to kMinPeriodSeconds..kMaxPeriodSeconds.
    static double interval(double periodSeconds);
    // The game handed an image over at `nowSeconds`: how long its render thread waits before going on.
    double afterHandOver(double periodSeconds, double nowSeconds);

private:
    std::optional<double> release_; // when the game was last let go on
};

// Per headset frame: how many images the game handed over since the previous headset frame, and how late
// the shown game frames were against the time their pose was predicted for.
class Cadence {
public:
    struct Counters {
        std::uint64_t frames = 0;
        std::uint64_t none = 0;     // no new image: the previous one is shown again
        std::uint64_t one = 0;      // exactly one new image
        std::uint64_t several = 0;  // two or more: all but the newest are never shown
        std::uint64_t notShown = 0; // images never shown
        std::uint64_t handOvers = 0;
        std::uint64_t shownViews = 0; // game frames shown for the first time, with a pose
        double lateSeconds = 0.0;     // their display time past their pose's predicted time, summed
    };

    // A headset frame began with `handOvers` images handed over in total so far.
    void frame(std::uint64_t handOvers);
    // A headset frame shows game frame `seq` (0: none) `lateSeconds` after the time its pose was predicted
    // for; each game frame counts once, at its first showing.
    void shown(std::uint64_t seq, double lateSeconds);

    [[nodiscard]] const Counters& counters() const { return counters_; }

private:
    std::optional<std::uint64_t> lastHandOvers_;
    std::uint64_t lastSeq_ = 0;
    Counters counters_;
};

} // namespace evr::pacing
