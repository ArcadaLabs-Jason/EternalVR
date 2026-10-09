#pragma once

// The headset's FOV over a session (docs/VR_HEAD_TRACKED.md). xrLocateViews gives each eye's FOV every frame;
// the mono path renders one symmetric FOV enclosing both, which was read once per session. Player logs showed
// first reads that were not the headset's: Virtual Desktop on a Quest 3 gave an eye -15/40/14/-26 degrees
// (kept as 80 x 52 for the session), a Steam Frame session half its usual FOV, a Rift S one 29 x 32 degrees.
// Rendered and shown at that FOV, the game is a tunnel. So the FOV is read again during the session:
//
// - A read is plausible when each eye spans at least kMinAcrossDegrees across and kMinUpDownDegrees up and
//   down, reaches at least kMinHalfDegrees and at most kMaxHalfDegrees from its centre on every side, is no
//   more lopsided than kMaxLopsided on either axis (the wider side over the narrower one), and the two eyes'
//   spans agree within kMaxEyeMismatch. The minimums leave room for a FOV narrowed on purpose (Quest Link's
//   FOV tangent multiplier at 0.5 to 0.6 gives about 56 to 64 degrees across), and the outer limit for the
//   widest headsets' outer side (a Pimax at its largest FOV, about 80 to 85 degrees).
// - The first plausible read is taken at once. A different one is taken once kStableReads reads in a row
//   agree on it (every angle within kSameDegrees), so a passing glitch never changes the FOV.
// - An implausible read is never taken: the last plausible FOV stays (before there is one the game keeps its
//   own FOV), and the FOV is read again.
// - Reads come every kReadSeconds, and every kRetrySeconds while nothing is taken yet or a change waits,
// until
//   kRetryReads implausible reads in a row: then every kReadSeconds again.
//
// No OpenXR here: FOVs in radians as XrFovf has them, times are the caller's seconds on a monotonic clock.

#include "xr_math/fov.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace evr::tracking {

using EyeFovs = std::array<xr_math::Fov, 2>; // eye L, eye R

// Why a read is not a headset's FOV.
enum class FovProblem : std::uint8_t {
    None,
    NotFinite,  // an angle is NaN or infinite
    Narrow,     // an eye spans less than the minimum across or up and down
    Edge,       // a side reaches less than kMinHalfDegrees from the centre, or more than kMaxHalfDegrees
    Lopsided,   // one side of an axis is more than kMaxLopsided times the other
    EyesDiffer, // the eyes' spans differ by more than kMaxEyeMismatch
};

inline constexpr float kMinAcrossDegrees = 50.0f;
inline constexpr float kMinUpDownDegrees = 45.0f;
inline constexpr float kMinHalfDegrees = 10.0f;
inline constexpr float kMaxHalfDegrees = 87.0f;
inline constexpr float kMaxLopsided = 2.5f;
inline constexpr float kMaxEyeMismatch = 0.2f; // relative to the wider eye's span

FovProblem fovProblem(const EyeFovs& eyes);
const char* fovProblemText(FovProblem problem);

// Every angle of both eyes within `degrees` of the other read's.
bool sameFovs(const EyeFovs& a, const EyeFovs& b, float degrees);

class FovWatch {
public:
    static constexpr int kStableReads = 3;
    static constexpr float kSameDegrees = 0.5f;
    static constexpr double kReadSeconds = 2.0;
    static constexpr double kRetrySeconds = 0.25;
    static constexpr int kRetryReads = 20;

    enum class Outcome : std::uint8_t {
        First,       // the first plausible read: taken
        Unchanged,   // the FOV taken
        Waiting,     // a different plausible FOV, not taken until it holds
        Changed,     // a different FOV held kStableReads reads: taken
        Implausible, // not taken (problem() says why)
    };

    // Whether a read is due at `seconds`.
    [[nodiscard]] bool due(double seconds) const { return seconds >= next_; }
    // A read at `seconds`. `check`: false takes every read as plausible (ETERNALVR_FOV_CHECK=0).
    Outcome onRead(const EyeFovs& eyes, double seconds, bool check = true);

    // The FOV taken; nullopt before the first plausible read.
    [[nodiscard]] const std::optional<EyeFovs>& taken() const { return taken_; }
    [[nodiscard]] FovProblem problem() const { return problem_; } // the last read's
    [[nodiscard]] std::uint64_t implausibleReads() const { return implausible_; }
    [[nodiscard]] std::uint64_t changes() const { return changes_; }

    // A new session: everything is forgotten and the next read is due at once.
    void reset() { *this = FovWatch{}; }

private:
    std::optional<EyeFovs> taken_;
    std::optional<EyeFovs> candidate_;
    int candidateReads_ = 0;
    FovProblem problem_ = FovProblem::None;
    std::uint64_t implausible_ = 0;
    int implausibleRun_ = 0; // implausible reads in a row
    std::uint64_t changes_ = 0;
    double next_ = 0.0;
};

} // namespace evr::tracking
