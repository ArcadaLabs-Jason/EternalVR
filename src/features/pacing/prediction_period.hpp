#pragma once

// How far past the runtime's predicted display time the camera hook predicts the head and hands: one display
// period (docs/VR_STEREO.md, "Pose"). While a runtime throttles or reprojects it reports a multiple of its
// refresh period (display_period_watch.hpp); player logs had periods of 22 to 56 ms, and the head was
// predicted that far ahead, so head motion overshot. The period added is held to kMaxBaseMultiple times the
// headset's refresh period when that is known. Not with the pose lead on (ETERNALVR_POSE_LEAD, on under
// ETERNALVR_PACE=headset): the lead measures how late frames are shown against the reported period, so a held
// period would only be added back by the lead, which then lags behind the next change.
//
// No OpenXR or Windows here.

#include <cstdint>

namespace evr::pacing {

constexpr double kMaxBaseMultiple = 1.5;

// The headset's refresh period for the limit, in ms: the runtime's own (XR_FB_display_refresh_rate, 0 when
// unknown), else the display period watch's base or, before it has one, its reference (0 when none yet).
double predictionBaseMs(double measuredBaseMs, double runtimeMs);

// The period to add, in ns: `reportedNs` itself, unless `poseLead` is off, `baseMs` is positive and
// `reportedNs` is over kMaxBaseMultiple times it; then kMaxBaseMultiple times the base. A reported period
// that is not positive is returned as it is.
std::int64_t predictionPeriodNs(std::int64_t reportedNs, double baseMs, bool poseLead);

} // namespace evr::pacing
