#pragma once

// How long Route S stays mono after a drain for a new tag base failed (docs/VR_STEREO.md, "Eye tags and
// pairing"). A drain holds the frontend for up to 250 ms, which the player feels as a hitch. When the
// render thread keeps failing to go idle (a map streaming in, a busy GPU), retrying every 2 s repeats that
// hitch every 2 s, so each failure in a row doubles the wait, up to kDrainRetryMaxMs; a drain that
// succeeds starts over from kDrainRetryMs.

#include <cstdint>

namespace evr::stereo_seq {

inline constexpr std::uint64_t kDrainRetryMs = 2000;
inline constexpr std::uint64_t kDrainRetryMaxMs = 16000;

// The mono wait after the `failures`-th failed drain in a row (1 for the first): kDrainRetryMs doubled
// per earlier failure, at most kDrainRetryMaxMs. 0 failures: kDrainRetryMs.
std::uint64_t drainRetryMs(std::uint32_t failures);

class DrainBackoff {
public:
    // A drain failed: the mono wait before the next one may run.
    std::uint64_t failed();
    // A drain took its base: the next failure waits kDrainRetryMs again.
    void succeeded() { failures_ = 0; }

    // Failed drains since the last one that succeeded.
    std::uint32_t failures() const { return failures_; }

private:
    std::uint32_t failures_ = 0;
};

} // namespace evr::stereo_seq
