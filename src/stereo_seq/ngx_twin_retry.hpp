#pragma once

// Per-eye DLSS after eye R's twin feature could not be created (vkcore/taa_ngx.hpp): DLSS falls back to TAA
// and eye R's feature is tried again later, instead of TAA for the rest of the session. A try holds
// r_antialiasing 2 again, which the player sees as a hitch (the render size changes), so tries are few: each
// failure in a row waits twice as long as the one before (kNgxTwinRetryMs, at most kNgxTwinRetryMaxMs), and
// after kNgxTwinRetries tries TAA stays until the player chooses DLSS in the game's video menu, which starts
// the count over. The game releasing a feature whose twin failed (it re-creates it for a new quality or size)
// ends the wait at once; that try still counts. No engine or NGX code here, so it is tested on every
// platform.

#include <cstdint>

namespace evr::stereo_seq {

inline constexpr std::uint64_t kNgxTwinRetryMs = 5000;
inline constexpr std::uint64_t kNgxTwinRetryMaxMs = 20000;
inline constexpr std::uint32_t kNgxTwinRetries = 3;

// The wait before the try that follows the `failures`-th failure in a row (1 for the first): kNgxTwinRetryMs
// doubled per earlier failure, at most kNgxTwinRetryMaxMs. 0 failures: kNgxTwinRetryMs.
std::uint64_t ngxTwinRetryMs(std::uint32_t failures);

class NgxTwinRetry {
public:
    // Eye R's feature could not be created at `nowMs`: DLSS falls back to TAA. During a fallback (the game's
    // other feature, before the next tick) it is the same try and counts once.
    void failed(std::uint64_t nowMs);
    // Every game feature has its twin again (the caller checks the others): true when this ends a fallback
    // or a try after one (DLSS per eye again after a failure).
    bool created();
    // Each stereo tick: true when a try is due at `nowMs` (the wait is over, the game released the failed
    // feature or the player asked for DLSS). The fallback ends; the caller lets eye R's next evaluation of
    // the failed feature create its twin again.
    bool due(std::uint64_t nowMs);
    // The player chose DLSS in the game's video menu: during a fallback, a try at the next tick with the
    // count started over. False (nothing changes) when DLSS has not fallen back.
    bool requested();
    // The game released a feature whose twin had failed: the next try is due at once (if one is left).
    void released();

    // DLSS falls back to TAA: a failure not tried again yet.
    bool fallback() const { return fallback_; }
    // During a fallback: no try is left until the player asks.
    bool exhausted() const { return fallback_ && failures_ > kNgxTwinRetries; }
    // Failures in a row (since eye R's feature was last created or the player last asked).
    std::uint32_t failures() const { return failures_; }

private:
    std::uint32_t failures_ = 0;
    std::uint64_t failedAtMs_ = 0;
    bool fallback_ = false;
    bool now_ = false;        // the next try is due at once
    bool recovering_ = false; // a failure since eye R's feature was last created
};

} // namespace evr::stereo_seq
