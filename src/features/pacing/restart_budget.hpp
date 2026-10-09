#pragma once

// How often a stalled frame clock (frame_clock_watch.hpp) may end the session to start a new one. A fixed
// three per game never came back: a player used two of the three within 100 s in two sessions, and a third
// stall later in the game would have been left as it is. So restarts are earned back after healthy play:
//
// - kMaxRestarts are available at the start.
// - One comes back for each kEarnSeconds of healthy frames (the session shown, no stall seen) since the last
//   restart or the last one earned, up to kMaxRestarts. Time between frames over kMaxGapSeconds (no session,
//   the worker held up) is not counted, and any stall, acted on or not, starts the healthy time over.
// - A hard limit holds whatever was earned: at most kMaxPerWindow restarts within any kWindowSeconds.
//
// No OpenXR or Windows here: times are the caller's seconds on a monotonic clock.

#include <array>
#include <cstddef>
#include <cstdint>

namespace evr::pacing {

class RestartBudget {
public:
    static constexpr std::uint32_t kMaxRestarts = 3;
    static constexpr double kEarnSeconds = 600.0;
    static constexpr double kMaxGapSeconds = 0.5;
    static constexpr std::size_t kMaxPerWindow = 6;
    static constexpr double kWindowSeconds = 3600.0;

    enum class Verdict : std::uint8_t {
        Restart,     // taken: the session may be restarted
        NoneLeft,    // every restart is used and none earned back yet
        RateLimited, // kMaxPerWindow restarts within kWindowSeconds already
    };

    // A frame the session showed with no stall, at `seconds`. True when it earned a restart back.
    bool onHealthyFrame(double seconds);
    // A stall seen at `seconds`: whether the session may be restarted (a Restart is counted as taken).
    Verdict onStall(double seconds);

    [[nodiscard]] std::uint32_t available() const { return available_; }
    [[nodiscard]] std::uint64_t taken() const { return taken_; }
    [[nodiscard]] double healthySeconds() const { return healthy_; }
    // Restarts within the window before `seconds`.
    [[nodiscard]] std::size_t recent(double seconds) const;

private:
    std::uint32_t available_ = kMaxRestarts;
    std::uint64_t taken_ = 0;
    double healthy_ = 0.0;
    double lastFrame_ = -1.0;
    std::array<double, kMaxPerWindow> times_{}; // the last kMaxPerWindow restarts, oldest overwritten
};

} // namespace evr::pacing
