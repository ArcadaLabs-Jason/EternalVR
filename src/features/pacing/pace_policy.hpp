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
// the headset's loop is not running (not shown, lost, stopping), so the game never hangs on it. A game that
// falls behind the headset is let go and runs as with `new` until it keeps up again (HoldSwitch, T-194), so
// the previous image is not submitted again at every refresh it misses.
//
// With `new` the game is not held at all; instead the headset's frame waits for the game: the XR worker
// begins a frame only once the game has handed over an image not shown yet (at most newFrameWaitSeconds), so
// every image is submitted once and the runtime sees the game's own rate, filling the refreshes in between
// with its reprojection, as for a native game below the refresh rate. Nothing re-presents the previous image
// except a wait that times out (the game stalled), which keeps the session's frames coming.
//
// FramePacer is the game thread's decision; Cadence counts, per headset frame, how many images were handed
// over since the one before (one each when paced; a mix of one and two when the game runs faster than the
// headset). No OpenXR or Windows here.

#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::pacing {

enum class PaceMode { Off, Headset, NewFrames };

// "off", "headset" or "new", any case, surrounding spaces ignored; nullopt otherwise.
std::optional<PaceMode> parsePaceMode(std::string_view text);
const char* paceModeName(PaceMode mode);

// PaceMode::NewFrames, and Headset while HoldSwitch has let the game go: how long the XR worker waits for a
// new image before it begins a headset frame anyway (kNewFrameWaitPeriods display periods, never more than
// kNewFrameMaxWaitSeconds; a period not known or not positive counts as 1/90 s). A wait ends at once when the
// image comes.
inline constexpr double kNewFrameWaitPeriods = 2.0;
inline constexpr double kNewFrameMaxWaitSeconds = 0.05;
double newFrameWaitSeconds(double periodSeconds);

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
    // past it the game runs free (a wait never reaches past this point either). Ten, not three: a streaming
    // runtime whose encoder falls behind stalls its loop for 40-100 ms at a time, and with three the game ran
    // free in every stall (a Virtual Desktop log: 158 such releases and 186 images never shown in 10 s at
    // 90 Hz), adding GPU work behind the runtime's own. About 110 ms at 90 Hz, still short of a hitch when
    // the loop really stops (headset off, a lost session).
    static constexpr double kIdlePeriods = 10.0;
    // ... and never longer than this, whatever the period reads (a runtime at 30 Hz would hold 333 ms).
    static constexpr double kMaxIdleSeconds = 0.12;

    struct Counters {
        std::uint64_t handOvers = 0;
        std::uint64_t waits = 0;         // waits that ended with a new headset frame
        std::uint64_t timeouts = 0;      // waits that ended without one (a miss)
        double waitSeconds = 0.0;        // the time spent in both kinds of wait
        double longestWaitSeconds = 0.0; // since the last takeLongestWait
        std::uint64_t frameBegun = 0;    // no wait: a headset frame had begun since the last one (the game is
                                         // at or below the headset's rate)
        std::uint64_t idle = 0;          // no wait: the headset's loop is not running
        std::uint64_t unseen = 0;        // no wait: held low, nobody sees the frames (unseen_hold.hpp)
    };

    explicit FramePacer(PaceMode mode = PaceMode::Off) : mode_(mode) {}

    // The game handed an image to the headset at `nowSeconds` (same clock as the loop's). `heldLow`: the low
    // hold while nobody sees the frames has just held it (T-228): no wait for a headset frame on top, which a
    // SYNCHRONIZED session may not begin at all (each image would add a timeout the stall watch counts).
    PaceStep afterHandOver(const HeadsetLoop& loop, double nowSeconds, bool heldLow = false);
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
// While nobody can see the frames (unseen_hold.hpp) the same clock holds it to kLowIntervalSeconds instead.
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
    // The same with the time between images given (not positive or not finite: no wait).
    double afterHandOverEvery(double intervalSeconds, double nowSeconds);
    // A wait ended early at `nowSeconds` (the hold changed): the next interval counts from here.
    void releasedAt(double nowSeconds);

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

// PaceMode::Headset (T-194, T-201): whether the game is held to the headset or let go to run as with `new`.
// Holding a game that cannot keep up with the headset leaves the previous image submitted again at every
// refresh it misses, and a held game whose headset frames also wait for its images falls into lock-step (each
// waits for the other, and display slots are lost: a rig A/B at 90 Hz, CPU-bound just under the refresh, ran
// 75 game frames a second against 85). So the game is held while it keeps up and let go once it does not:
// over each kWindowSeconds, held, more than kLetGoRepeatShare of the headset frames without a new image is a
// bad window, and kBadWindowsToLetGo bad windows in a row let it go (one hitch above the refresh, 50 ms,
// makes one bad window); let go, the game's own rate (its images handed over per second) reaching
// kHoldRateShare of the refresh rate holds it again, once it has been let go for at least the back-off. A
// game that runs faster than the headset on its own and still repeats once held (its frame takes about one
// display period; rig: 105 game frames a second let go, 85 held at 90 Hz) would switch over and over, so a
// hold that ends within kLastingHoldSeconds doubles the back-off (1, 2, 4 ... kMaxBackOffSeconds); only a
// hold that lasts that long starts it over at one window. A window longer than kMaxWindowSeconds (the
// headset's loop stopped: sleep, a session not running, a stalled copy) or with fewer than kMinWindowFrames
// headset frames decides nothing and breaks a run of bad windows. Away from play (menus, loading screens:
// `inPlay` false) the game is held and nothing is decided, so the menu pointer and the loading screen stay at
// the headset's rate.
class HoldSwitch {
public:
    static constexpr double kWindowSeconds = 1.0;
    static constexpr double kLetGoRepeatShare = 0.05;
    static constexpr int kBadWindowsToLetGo = 2;
    static constexpr double kHoldRateShare = 1.0;
    static constexpr double kLastingHoldSeconds = 10.0;
    static constexpr double kMaxBackOffSeconds = 32.0;
    static constexpr double kMaxWindowSeconds = 2.0 * kWindowSeconds;
    static constexpr std::uint64_t kMinWindowFrames = 30;

    // Why held() last changed.
    enum class Change { None, FellBehind, KeptUp, LeftPlay };

    // Held, the game's own time for an image (from its release after the previous hand-over to this
    // hand-over, seconds; release-to-present timing). With the latency gate on (setLatencyGate), a window
    // whose repeats would let the game go does so only when more than half of its images took longer than the
    // display period: a game that draws its images within the period but repeated (a hitch, other work on the
    // PC) stays held. Windows without such times decide as without the gate.
    void noteGameTime(double seconds);
    void setLatencyGate(bool on) { latencyGate_ = on; }

    // A headset frame began at `nowSeconds`, the game having handed over `handOvers` images in total so far;
    // `periodSeconds`, the runtime's display period (not positive or not finite: not known, no decision);
    // `inPlay`, the game is in play (not a menu or a loading screen). True when it changed held().
    bool frame(double nowSeconds, std::uint64_t handOvers, double periodSeconds, bool inPlay = true);
    // A headset frame while the layer holds the game to a few images a second because nobody can see them
    // (unseen_hold.hpp, T-228): the game's rate is the hold's, so nothing is decided and held() stays as it
    // was; the window starts over and a run of bad windows is broken, so the first window after the hold
    // judges the game alone.
    void pause(double nowSeconds, std::uint64_t handOvers);

    [[nodiscard]] bool held() const { return held_; }
    [[nodiscard]] std::uint64_t switches() const { return switches_; }
    [[nodiscard]] double backOffSeconds() const { return backOff_; }
    // The last change, and the last window that decided: its share of headset frames without a new image and
    // the game's rate (images handed over per second).
    [[nodiscard]] Change lastChange() const { return lastChange_; }
    [[nodiscard]] double lastRepeatShare() const { return lastRepeatShare_; }
    [[nodiscard]] double lastRate() const { return lastRate_; }

private:
    bool held_ = true;
    std::uint64_t switches_ = 0;
    double backOff_ = kWindowSeconds;
    double changedAt_ = 0.0; // when held_ last changed (the first frame's time until then)
    bool holdTried_ = false; // held again by the game's rate (not by a menu): a let-go judges that hold
    int badWindows_ = 0;     // bad windows in a row, held
    Change lastChange_ = Change::None;
    double lastRepeatShare_ = 0.0;
    double lastRate_ = 0.0;
    std::optional<std::uint64_t> lastHandOvers_; // at the previous headset frame
    std::optional<double> windowStart_;
    std::uint64_t windowHandOvers_ = 0; // at the window's start
    std::uint64_t frames_ = 0;          // headset frames in the window
    std::uint64_t repeats_ = 0;         // ... of them without a new image
    bool latencyGate_ = false;
    std::uint64_t gameTimes_ = 0;     // images with a game time in the window
    std::uint64_t slowGameTimes_ = 0; // ... of them over the display period
    double lastPeriod_ = 0.0;         // the display period at the previous headset frame
};

} // namespace evr::pacing
