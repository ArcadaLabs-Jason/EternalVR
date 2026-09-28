#pragma once

// Hand-aim smoothing (ETERNALVR_AIM_SMOOTHING, docs/VR_CONTROLLERS.md): a one-euro filter (Casiez, Roussel
// and Vogel, CHI 2012) on the weapon hand's aim orientation. The gun, the shots and the reticle all take
// the filtered ray, so they stay together.
//
// A one-euro filter is a first-order low-pass whose cutoff rises with speed: minCutoffHz while the hand
// is still (tremor and tracking noise are cut, and the reticle 10 m out stops shimmering), plus beta per
// radian per second of the hand's angular speed, so a moving hand is followed with a few milliseconds of
// lag. The speed is itself low-passed at derivativeCutoffHz, so tremor's back-and-forth does not open the
// filter. The speed is the angular-velocity vector's length, which averages tremor out.
//
// One strength from 0 to 1 is offered: 0 is off, 1 the strongest (most smoothing, most lag).

#include "common/quat.hpp"
#include "common/vector.hpp"

#include <optional>

namespace evr::input {

inline constexpr float kDefaultAimSmoothing = 0.3f;

struct OneEuroParams {
    float minCutoffHz = 1.0f;
    float beta = 0.0f; // cutoff added per radian per second of angular speed (Hz)
    float derivativeCutoffHz = 1.0f;
};

// The parameters for a strength in [0, 1] (clamped); nullopt for 0 (off) or a non-finite strength.
// minCutoffHz falls from 8 Hz toward 0.5 Hz at 1, beta from 20 toward 10 Hz per rad/s.
std::optional<OneEuroParams> aimSmoothingParams(float strength);

// Filters one orientation over time. Samples with the same time as the last (several game frames
// predicted for one display time) return the last output unchanged; a gap longer than kResetSeconds, or
// the first sample, passes through unfiltered.
class OneEuroRotation {
public:
    static constexpr double kResetSeconds = 0.25;

    explicit OneEuroRotation(OneEuroParams params) : params_(params) {}

    Quat update(Quat raw, double seconds);
    // Forget the history (tracking lost); the next sample passes through.
    void reset() { primed_ = false; }

private:
    OneEuroParams params_;
    bool primed_ = false;
    double lastSeconds_ = 0.0;
    Quat lastRaw_;
    Quat out_;
    Vec3 angularVelocity_{}; // filtered, radians per second
};

} // namespace evr::input
