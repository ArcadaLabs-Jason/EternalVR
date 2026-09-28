#pragma once

// Bringing VR back after the headset or its runtime went away (presenter_reconnect.cpp): which losses are
// recovered from, how often the XR worker tries again, and which attempts it logs. Pure, so the rules are
// tested without a runtime.

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

} // namespace evr::xr_recovery
