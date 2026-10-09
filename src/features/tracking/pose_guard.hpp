#pragma once

// A tracked position that jumped further than a head or a hand can move (docs/VR_ROOMSCALE.md). Runtimes give
// poses with their valid bits set that are not where the device is: player logs had an eye 2.90 m from the
// head (a Steam Frame) and positions 9.9 and 12.7 m away (WMR). The stereo path renders such a frame mono,
// but the head path fed the jump into body follow and the lean cap, and a hand's velocity into the punch and
// the hands-up jump. So each tracked position goes through a guard:
//
// - A position further from the last good one than the limits' minJumpMetres plus their speed times the time
//   since it is held: the last good position is used instead. The orientation is the caller's (never held).
// - A new place is taken once the positions there have agreed with each other (within the same limits) for
//   kSettleSeconds: a teleport, or a recenter no event announced. Positions that jump about are never taken
//   as a place, but the reach from the last good one grows with the time held, so no hold lasts for long
//   (a head 10 m away is within reach after about 1.6 s).
// - A time up to kClockStepBackSeconds before the last good one counts as the same time; further back is a
//   new clock (the last good position is forgotten).
// - reset() forgets the last good position: the caller resets on a runtime recenter, a reference space change
//   and a new session, so their jumps are taken at once.
//
// No OpenXR here: positions in metres in one tracking space, times the caller's seconds (pose times).

#include "common/vector.hpp"

#include <optional>

namespace evr::tracking {

struct PoseLimits {
    float minJumpMetres = 0.0f;   // always allowed between two positions
    float metresPerSecond = 0.0f; // and this much more per second between them
};

// A head moves well under 3 m/s (a quick duck or lean); a hand in a punch about 10 m/s, its predicted
// position swings further, and a controller coming back into the cameras' view can snap by tens of
// centimetres. Between frames at 90 Hz that allows 0.32 m for the head and 0.72 m for a hand; the jumps seen
// were metres.
inline constexpr PoseLimits kHeadLimits{0.25f, 6.0f};
inline constexpr PoseLimits kHandLimits{0.5f, 20.0f};
inline constexpr double kSettleSeconds = 0.5;
inline constexpr double kClockStepBackSeconds = 0.005;
// A hand velocity over this is not a hand's (the punch threshold tops out at 4 m/s).
inline constexpr float kMaxHandMetresPerSecond = 25.0f;

// Whether a hand's linear velocity is finite and below kMaxHandMetresPerSecond.
bool plausibleHandVelocity(Vec3 velocity);

struct GuardStep {
    Vec3 position;                 // the position to use
    bool held = false;             // the last good position, in place of the one given
    bool taken = false;            // a jump taken after a hold: it settled for kSettleSeconds
    bool released = false;         // back within the limits after a hold
    float jumpMetres = 0.0f;       // held or taken: how far the given position was from the last good one
    double heldSeconds = 0.0;      // held, taken or released: how long the hold has lasted
    double sinceGoodSeconds = 0.0; // held: the time since the last good position
};

class PoseGuard {
public:
    explicit PoseGuard(PoseLimits limits) : limits_(limits) {}

    // One position at `seconds`. A position or time that is not finite is passed through as it is.
    GuardStep update(Vec3 position, double seconds);
    // Forgets the last good position: the next one is taken as it is.
    void reset();

    [[nodiscard]] bool holding() const { return holdSince_ >= 0.0; }

private:
    void take(Vec3 position, double seconds);
    [[nodiscard]] bool reachable(Vec3 from, double fromSeconds, Vec3 to, double toSeconds) const;

    PoseLimits limits_;
    std::optional<Vec3> last_;
    double lastSeconds_ = 0.0;
    std::optional<Vec3> candidate_; // the newest position away from the last good one, while held
    double candidateSince_ = 0.0;   // when the positions agreeing with it began
    double candidateSeconds_ = 0.0;
    double holdSince_ = -1.0;
};

} // namespace evr::tracking
