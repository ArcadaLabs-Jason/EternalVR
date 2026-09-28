#pragma once

// How far past the next display time the camera hook predicts the head and the hands
// (docs/VR_HEAD_TRACKED.md, docs/rig-findings/aim-jitter.md).
//
// The camera hook predicts for the XR frame after the one being submitted (predictedDisplayTime plus one
// period). A game frame is shown once it has been rendered and copied, which at 44-70 frames per second
// on a Quest 3 at 90 Hz was two periods after that time for most frames, and one period at 85-120. The
// compositor corrects the head for the difference, but not the gun: the weapon was drawn where the hand
// was that much earlier. The lead is the measured lateness of each view's first showing, followed with a
// small gain, so that on average a view is predicted for the time it is shown.
//
// Each view counts once, at its first showing; repeats of the same view are late by design (the game had
// no new frame) and prediction cannot help them. A single sample moves the lead by at most kGain periods,
// so a hitch does not throw it, and the lead stays within [0, kMaxPeriods periods]: predicting further
// makes the runtime extrapolate the hands and head too far. Used under ETERNALVR_POSE_LEAD=1 only.

#include <cstdint>

namespace evr::xr_math {

class DisplayLead {
public:
    static constexpr double kGain = 0.05;
    static constexpr double kMaxPeriods = 2.0;

    // An XR frame shows view `seq` (non-zero) at `lateNs` past the time its poses were predicted for
    // (predictedDisplayTime - poseTime); `periodNs` is the display period.
    void noteShown(std::uint64_t seq, std::int64_t lateNs, std::int64_t periodNs);

    // Nanoseconds to add to predictedDisplayTime + one period.
    [[nodiscard]] std::int64_t leadNs() const { return static_cast<std::int64_t>(lead_); }

    void reset() {
        lead_ = 0.0;
        lastSeq_ = 0;
    }

private:
    double lead_ = 0.0;
    std::uint64_t lastSeq_ = 0;
};

} // namespace evr::xr_math
