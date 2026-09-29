#pragma once

// Adaptive alternate eyes (ETERNALVR_ALTERNATE_EYES=auto, docs/rig-findings/alternate-eye.md section 5):
// Route S (both eyes in every game tick) while the processor keeps up with the headset's display rate,
// alternate eyes (one eye per tick) while it does not.
//
// Both ways keep the render order L, R, L, R and "eye R follows its eye L", and each eye's temporal state
// (previous matrices, TAA and DLSS history, the object ring) is keyed by the eye and its own last render, so
// the way a pair renders can change at any pair's start (an eye L render) without a reset. The present hook
// pairs a same-tick eye R with its eye L again (AlternatePairing, pairInTick tags).
//
// AdaptiveEyes decides from the wall time between the engine chain's frame ends (a tick). While pairs render
// both eyes it switches to alternation after about a second of ticks below the display rate. While
// alternating it estimates what a tick with both eyes would take (the alternating tick plus eye R's render,
// scaled by eye R's measured share of a Route S tick) and switches back after about three seconds of that
// estimate comfortably above the display rate. Going back to alternation soon after switching back doubles
// the next wait (up to a limit), so a machine on the edge does not flip every few seconds.
//
// No engine, Vulkan or OpenXR here, so it is tested on every platform.

#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::stereo_seq {

// ETERNALVR_ALTERNATE_EYES.
enum class AlternateMode : std::uint8_t {
    Off,  // Route S: both eyes in every tick (the default)
    Auto, // Route S while the processor keeps up with the headset, alternate eyes while it does not
    On,   // alternate eyes: one eye per tick
};

// "auto" Auto; "1", "on", "true" On (any case, as switchValue); anything else, unset and empty included, Off.
AlternateMode alternateMode(std::string_view value);
const char* alternateModeName(AlternateMode mode);

// One game tick as the frame-end wrapper saw it.
struct TickSample {
    double seconds = 0.0;      // wall time from the tick before's frame end to this one's
    bool stereo = false;       // a stereo tick (false: mono, a menu, a loading screen, a drain)
    bool pairedTick = false;   // both eyes rendered in the tick (Route S); false: one eye (alternating)
    double rightSeconds = 0.0; // pairedTick: the wall time of its nested eye R render
};

struct AdaptiveConfig {
    double window = 0.25;   // seconds of ticks per measurement
    double onBelow = 0.97;  // alternate when Route S ticks per second fall below the display rate times this
    double onAfter = 1.0;   // for this long without a break
    double offAbove = 1.15; // pair again when the estimated Route S rate is above the display rate times this
    double offAfter = 3.0;  // for this long without a break
    double flapWithin = 10.0;  // alternating again this soon after pairing again doubles the next offAfter
    double offAfterMax = 24.0; // up to this
    double shareStart = 0.8;   // eye R's render / the rest of a Route S tick, until measured
    double shareMin = 0.25;    // the measured share is clamped to [shareMin, shareMax]
    double shareMax = 2.0;
    double shareWeight = 0.25; // each Route S window moves the share this far towards its own
    double defaultHz = 90.0;   // the display rate while the headset has not given one
};

class AdaptiveEyes {
public:
    explicit AdaptiveEyes(AdaptiveConfig config = {});

    // The headset's display period (XrFrameState::predictedDisplayPeriod) in seconds; anything outside 1/1000
    // to 1/20 s keeps the rate from before (the default until one is given).
    void setDisplayPeriod(double seconds);
    double displayHz() const { return hz_; }

    // True: a pair that starts now renders both eyes in its tick (Route S); false: it alternates. Starts
    // true.
    bool pairs() const { return pairs_; }

    struct Switch {
        bool toAlternate = false;
        double rate = 0.0;     // ticks per second in the window that decided
        double estimate = 0.0; // Route S ticks per second: measured (to alternate) or estimated (back)
        double hz = 0.0;       // the display rate
        double after = 0.0;    // seconds the condition held
        double share = 0.0;    // eye R's share used for the estimate
    };
    // A tick's frame end; the switch it decided, if any. The new way applies from the next pair's start.
    std::optional<Switch> onTick(const TickSample& tick);

    struct Stats {
        double seconds[2] = {};         // stereo tick time: [0] both eyes per tick, [1] alternating
        std::uint64_t ticks[2] = {};    // stereo ticks, the same split
        double monoSeconds = 0.0;       // mono tick time
        std::uint64_t switches[2] = {}; // [0] back to both eyes per tick, [1] to alternation
    };
    const Stats& stats() const { return stats_; }
    double share() const { return share_; }
    double lastRate() const { return lastRate_; }         // ticks per second in the last closed window
    double lastEstimate() const { return lastEstimate_; } // Route S ticks per second (measured or estimated)
    double offAfter() const { return offAfter_; }         // the wait before pairing again, now

private:
    void resetWindow();

    AdaptiveConfig c_;
    bool pairs_ = true;
    double hz_;
    double now_ = 0.0; // seconds of ticks seen
    double winSeconds_ = 0.0;
    double winRight_ = 0.0;
    std::uint64_t winTicks_ = 0;
    double streak_ = 0.0; // seconds the switch condition has held
    double share_;
    double offAfter_;
    double lastPaired_ = -1.0e9; // now_ when pairs last started again
    double lastRate_ = 0.0;
    double lastEstimate_ = 0.0;
    Stats stats_;
};

// ETERNALVR_TEST_CPU_LOAD_MS (a test knob, never set by the launcher): "<ms>[,<seconds on>,<seconds off>]"
// busy-waits ms milliseconds at every render's frame end, as a slower processor would take; with the two
// periods the load is on for the first, off for the second, and so on from the first render. ms is 0 to
// 100 (fractions allowed); anything else is no load.
struct CpuLoadSpec {
    double ms = 0.0;
    double onSeconds = 0.0; // 0: always on
    double offSeconds = 0.0;
};
std::optional<CpuLoadSpec> parseCpuLoad(std::string_view value);
// The load in milliseconds at `seconds` after the first render.
double cpuLoadMsAt(const CpuLoadSpec& spec, double seconds);

} // namespace evr::stereo_seq
