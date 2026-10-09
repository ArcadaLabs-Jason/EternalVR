#pragma once

// Bringing VR back after the headset or its runtime went away (presenter_reconnect.cpp): which losses are
// recovered from, how often the XR worker tries again, which attempts it logs, and what follows a failed
// xrBeginSession. Pure, so the rules are tested without a runtime.

#include <cstdint>

namespace evr::xr_recovery {

// Why the session ended.
enum class Loss : std::uint8_t {
    Session,  // XR_SESSION_STATE_LOSS_PENDING or XR_ERROR_SESSION_LOST: the headset went away
    Instance, // an instance loss event, XR_ERROR_INSTANCE_LOST or XR_ERROR_RUNTIME_FAILURE: the runtime
              // stopped
    Exiting,  // XR_SESSION_STATE_EXITING: the runtime asked the application to leave VR
};

// For the log.
inline const char* lossName(Loss loss) {
    switch (loss) {
    case Loss::Session:
        return "session lost";
    case Loss::Instance:
        return "runtime lost";
    case Loss::Exiting:
        return "exiting";
    }
    return "?";
}

// Whether VR is brought back after the loss. EXITING is the runtime, or the player through it, closing the
// application's VR; coming back at once would fight that, so the game stays flat as before.
constexpr bool recovers(Loss loss) {
    return loss != Loss::Exiting;
}

// Milliseconds to wait before reconnect attempt `attempt` (0 is the first): 1 s, 2 s, 3 s, 4 s, 5 s, then
// every 5 s, so a headset back within seconds is found quickly and a long outage costs little.
constexpr std::uint32_t retryDelayMs(std::uint32_t attempt) {
    return attempt < 5 ? (attempt + 1) * 1000 : 5000;
}

// Whether attempt `attempt` is logged: the first five, then one a minute.
constexpr bool logsAttempt(std::uint32_t attempt) {
    return attempt < 5 || (attempt - 5) % 12 == 0;
}

// xrBeginSession failing in READY (presenter_xr_events.cpp). Each READY gets kBeginTries calls,
// kBeginRetryMs apart; then the session is made again as after a loss, at once when the call returned a loss
// (the session or instance lost, a runtime failure). Either way at most kBeginRestarts times a game: past
// that the game stays flat, until the runtime makes the session READY again after a failure without a loss,
// for the rest of the game after a loss (no reconnect is tried).
inline constexpr std::uint32_t kBeginTries = 4;
inline constexpr std::uint32_t kBeginRetryMs = 2000;
inline constexpr std::uint32_t kBeginRestarts = 2;

enum class BeginNext : std::uint8_t {
    Retry,   // call xrBeginSession again after kBeginRetryMs
    Restart, // take the session as lost: the worker makes a new one (presenter_reconnect.cpp)
    GiveUp,  // stay flat
};

// What follows the `failures`-th failed call (1 is the first) of this READY, with `restarts` sessions made
// again for it already this game; `lost`: the call returned a loss.
constexpr BeginNext afterBeginFailure(std::uint32_t failures, std::uint32_t restarts, bool lost = false) {
    if (!lost && failures < kBeginTries) {
        return BeginNext::Retry;
    }
    return restarts < kBeginRestarts ? BeginNext::Restart : BeginNext::GiveUp;
}

} // namespace evr::xr_recovery
