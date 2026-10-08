#pragma once

// SSDO's temporal history per eye (vkcore/ssdo_hooks.hpp; Steam build 25216728).
//
// The ambient occlusion is filtered over time (r_SSDOTemporalAA): each render writes one of two half-size
// accumulation targets and reads the other as its history. The engine keeps them with the unfiltered target
// in one array of the device context (+0x550 / +0x558, unfiltered +0x560): it writes [counter & 1] and reads
// [(counter & 1) ^ 1]. With two renders per tick eye L always gets one parity and eye R the other, so each
// eye would filter its occlusion with the other eye's: the stereo set holds the filter off.
//
// Here each eye has a pair of its own (eye L and mono the engine's two targets, eye R two more) and
// alternates within it by its own renders. Before each render the plan gives the engine's array for it: the
// eye's next target where the engine writes, its last written one where the engine reads. The engine's filter
// state (one block per view slot) is shared: it resets the filter itself when its last frame is not the
// render just before, so the plan only says when an eye's own history is stale and the hook makes the engine
// reset:
// - the eye's first render, and its first after a resize;
// - its own last render is neither the render before (mono renders, eye L after a skipped eye R) nor the one
//   two back (the other eye's in between);
// - eye L, when eye R's last render is not the render just before: eye R missed the last tick (or stereo just
//   started), so both eyes start over this tick, eye R by the rule above;
// - a mono render right after an eye L render: eye R's render whose tag was not found, which must not filter
//   with eye L's history (eye L then starts over at its next render by the rule above).
// Mono renders otherwise count as eye L and never check eye R.

#include "stereo_seq/eye_tags.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace evr::stereo_seq {

struct SsdoPair {
    void* target0 = nullptr;
    void* target1 = nullptr;
    friend constexpr bool operator==(SsdoPair, SsdoPair) = default;
};

enum class SsdoRestart : std::uint8_t {
    None,
    First,      // the eye's first render
    Resize,     // its first render after the targets were resized
    Gap,        // its own last render is not one or two renders back
    EyeRMissed, // eye L: eye R's last render is not the render just before
    Untagged,   // a mono render right after an eye L render (eye R's, without its tag)
    Resumed,    // its first render after dynamic resolution held the history off
};
inline constexpr int kSsdoRestarts = 7;

// How many distinct arrays a plan can ask for (eye x the pair's target written x the counter's parity).
inline constexpr int kSsdoArrays = 8;

struct SsdoPlan {
    // The engine's array for this render: [counter & 1] is written, the other one read, [2] the unfiltered
    // target.
    std::array<void*, 3> targets{};
    // Which of kSsdoArrays arrays the hook keeps these in: the same key always holds the same targets (until
    // a reset of the history), so a pass of an earlier render that reads its array late reads what it was
    // given.
    int key = 0;
    SsdoRestart restart = SsdoRestart::None;
};

class SsdoHistory {
public:
    // The engine's pair as it built it (eye L's), eye R's and the unfiltered target; forgets every eye's
    // renders.
    void reset(const SsdoPair& engine, const SsdoPair& eyeR, void* unfiltered);
    bool ready() const { return ready_; }
    // The targets were resized or written outside the history (their contents are gone): each eye starts over
    // at its next render, for `why`.
    void invalidate(SsdoRestart why = SsdoRestart::Resize);

    // Before a render of `eye` with the engine's backend frame counter `counter`.
    SsdoPlan beforeRender(Eye eye, std::uint32_t counter);

private:
    struct PerEye {
        SsdoPair pair{};
        int written = -1; // the target of its pair its last render wrote; -1 before its first
        std::optional<std::uint32_t> lastRender;
        Eye lastEye = Eye::Mono;
        SsdoRestart pending = SsdoRestart::None; // the reason of the next first render (invalidate)
    };
    std::array<PerEye, 2> eyes_{};
    void* unfiltered_ = nullptr;
    bool ready_ = false;
};

const char* ssdoRestartName(SsdoRestart restart);

// What the per-eye history needs before SSDO's filter may run in stereo.
struct SsdoReadiness {
    bool requested = false;         // ETERNALVR_STEREO_SSDO_TAA (default on)
    bool installed = false;         // the three hooks
    bool gameTouch = false;         // the multiplayer guard allows game writes
    bool failedClosed = false;      // a check after start-up failed (sticky)
    bool targetsMade = false;       // eye R's targets were made with the device context, of the engine's size
    bool dynamicResolution = false; // rs_enable reads non-zero: the engine sizes the targets by parity itself
};

// Why the filter must stay off, or nullptr when it may run.
const char* ssdoNotReady(const SsdoReadiness& readiness);

} // namespace evr::stereo_seq
