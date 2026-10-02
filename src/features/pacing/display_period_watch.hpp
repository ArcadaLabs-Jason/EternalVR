#pragma once

// The headset's display period over a session (docs/VR_STEREO.md "Headset refresh rate"). The runtime's
// XrFrameState::predictedDisplayPeriod is 1/refresh, but several runtimes report a multiple of it while they
// hold the game at a fraction of the refresh rate (Virtual Desktop's SSW, SteamVR's throttling and Motion
// Smoothing, Pimax Smart Smoothing), and some headsets change their refresh rate during play. A 10 s snapshot
// hides flips inside the window, so every frame's period goes through this watch:
//
// - A new period counts once it holds for kHoldFrames frames in a row (one odd frame is not a change);
//   periods within kSameTolerance of each other are the same period (SteamVR measures its period, so it
//   wanders a little).
// - The time between frames is added to the settled period (gaps over kMaxFrameGapSeconds, a session not
//   running, are not counted).
// - The base is the shortest period that held for kBaseHoldSeconds: the headset's refresh rate as far as the
//   period shows it. A settled period that is about 2x or 3x the base is the runtime throttling or
//   reprojecting; one that is no multiple of it is a refresh change.
//
// No OpenXR or Windows here: times are the caller's seconds on any monotonic clock.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace evr::pacing {

// A settled period and the time spent at it.
struct PeriodTime {
    double periodMs = 0.0;
    double seconds = 0.0;
};

// A period that just settled.
struct PeriodChange {
    double fromMs = 0.0;      // the period settled before; 0 for the first one
    double toMs = 0.0;        // the new one (the average of the frames that settled it)
    double atSeconds = 0.0;   // when the first of those frames came
    double referenceMs = 0.0; // the watch's referenceMs() before this change (0: none yet)
};

class DisplayPeriodWatch {
public:
    static constexpr int kHoldFrames = 6;
    static constexpr double kSameTolerance = 0.03; // relative
    static constexpr double kBaseHoldSeconds = 3.0;
    static constexpr double kMaxFrameGapSeconds = 0.5;
    static constexpr std::size_t kMaxPeriods = 32; // distinct periods kept; later ones join the nearest

    // One XR frame: its predicted display period in ms and when it came. A change when a new period settled.
    std::optional<PeriodChange> onFrame(double periodMs, double nowSeconds);

    [[nodiscard]] double settledMs() const { return settled_; } // 0 until a period settled
    [[nodiscard]] double baseMs() const { return base_; }       // 0 until a period held kBaseHoldSeconds
    // The base, or until there is one the shortest period settled so far (0: none).
    [[nodiscard]] double referenceMs() const;
    [[nodiscard]] std::uint64_t changes() const { return changes_; } // settled changes after the first period
    [[nodiscard]] const std::vector<PeriodTime>& times() const { return times_; }

private:
    void addTime(double seconds);

    double settled_ = 0.0;
    double base_ = 0.0;
    double shortest_ = 0.0;
    double runSeconds_ = 0.0; // the settled period's time since it settled
    double lastFrame_ = -1.0;
    double candidate_ = 0.0;
    double candidateSum_ = 0.0;
    double candidateStart_ = 0.0;
    int candidateFrames_ = 0;
    std::uint64_t changes_ = 0;
    std::vector<PeriodTime> times_;
};

// Whether two periods are the same period (within DisplayPeriodWatch::kSameTolerance).
bool samePeriod(double a, double b);

// k when `periodMs` is about k times `referenceMs` (k >= 1, within kMultipleTolerance of the ratio); 0 when
// it is no whole multiple, or either is not positive. Summaries count multiples up to kMaxMultiple (SteamVR
// throttles down to 1/6), anything longer as other.
constexpr double kMultipleTolerance = 0.08;
constexpr int kMaxMultiple = 6;
int multipleOf(double periodMs, double referenceMs);

// The refresh rate of a period, in Hz (0 for a period that is not positive).
double hertz(double periodMs);

// The base a summary uses: the measured base, or the runtime's own refresh period
// (XR_FB_display_refresh_rate, 0 when unknown) when that is shorter and the measured base a multiple of it
// (a session that ran throttled from its start never shows the refresh period itself).
double summaryBase(double measuredBaseMs, double runtimeMs);

// The change line after "xr: ", for example "display period 6.94 -> 13.89 ms at 312.4 s (2x the base
// 6.94 ms: the runtime is throttling or reprojecting)". The change is judged against the runtime's own
// refresh period when it gives one (`runtimeMs`, XR_FB_display_refresh_rate; 0 when unknown), else against
// the change's referenceMs.
std::string changeText(const PeriodChange& change, double runtimeMs);

// The time at each period as shares of the whole, by multiple of the base.
struct RefreshShares {
    double baseMs = 0.0;
    double totalSeconds = 0.0;
    std::vector<double> multiples;  // [k - 1]: the share at k times the base
    double other = 0.0;             // at periods that are no multiple of it
    std::vector<PeriodTime> others; // those periods, longest time first
    // The share at 2x or more: the runtime throttling or reprojecting.
    [[nodiscard]] double throttled() const;
};
RefreshShares shares(const std::vector<PeriodTime>& times, double baseMs);

// The summary after "xr: refresh summary: ", for example "base 6.94 ms (144 Hz); 1x 81.5%, 2x 17.2%, 3x
// 0.9%, other 0.4% (90 Hz 0.4%); 12 change(s) in 312 s". Empty when no time was counted.
std::string summaryText(const RefreshShares& shares, std::uint64_t changes);

// The status file's values: refresh_hz from the base period ("144.0"; empty for none) and throttled_share,
// the share of time at 2x the base or more ("0.181"; empty without any counted time).
std::string refreshHzValue(double baseMs);
std::string throttledShareValue(const RefreshShares& shares);

} // namespace evr::pacing
