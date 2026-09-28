#pragma once

// Posture while playing: noticing that a seated player stood up or a standing player sat down
// (docs/VR_ROOMSCALE.md, Posture re-detection).
//
// The anchor detects the posture once, from the head's height above the floor (posture_detector.hpp).
// Players stand up and sit down during a session, so the tracker keeps watching that height and reports
// a change once the head has stayed in the other posture's range long enough: above
// `standingAboveMetres` for `standingSeconds` while seated, below `seatedBelowMetres` for
// `seatedSeconds` while standing. The head must also have moved at least `minChangeMetres` from the
// height the posture was detected at, so a tall player detected seated near the top of the seated band
// does not flip by sitting up straight. A dip or rise shorter than the dwell (ducking, a stretch) never
// flips it.
//
// The tracker only watches: the caller re-anchors the height on a change and resets the tracker after
// every anchor. With the posture unknown (no floor space, or not anchored) it reports nothing.

#include "features/posture/posture_detector.hpp"

#include <optional>

namespace evr::posture {

struct PostureTrackerSettings {
    float standingAboveMetres = 1.35f;
    float seatedBelowMetres = 1.20f;
    float minChangeMetres = 0.30f;
    float standingSeconds = 1.0f;
    float seatedSeconds = 1.5f;
};

struct PostureChange {
    Posture from = Posture::Unknown;
    Posture to = Posture::Unknown;
    float heightMetres = 0.0f; // the head's height above the floor when the change was decided
    float heldSeconds = 0.0f;  // how long it had been in the new posture's range
};

class PostureTracker {
public:
    // Settings that are not finite and sane (thresholds 0.5 to 2.5 m with seated below standing, change
    // 0.05 to 1 m, dwells 0.1 to 10 s) fall back to the defaults as a whole.
    explicit PostureTracker(PostureTrackerSettings settings = {});

    // After an anchor: the posture in force and the head's height above the floor it was taken at.
    // Unknown (or no height) stops the tracking until the next reset.
    void reset(Posture current, std::optional<float> anchorHeightMetres);

    // One head-height sample (metres above the floor; nullopt or non-finite: no reading, which neither
    // counts toward nor breaks a dwell) at `seconds` (monotonic; going backwards restarts a dwell).
    // Returns the change on the sample that completes a dwell; the tracker is then in the new posture,
    // referenced to that sample's height.
    std::optional<PostureChange> update(std::optional<float> headAboveFloorMetres, double seconds);

    [[nodiscard]] Posture current() const { return current_; }
    // A dwell toward the other posture is running.
    [[nodiscard]] bool pending() const { return candidate_ != Posture::Unknown; }
    // The last height read is on its way to the other posture: more than half the minimum change away
    // from the reference height, toward it. A seated player rising out of the chair is changing before
    // the dwell starts (the caller holds back the lean-cap fade meanwhile).
    [[nodiscard]] bool changing() const { return changing_; }
    [[nodiscard]] const PostureTrackerSettings& settings() const { return settings_; }

private:
    PostureTrackerSettings settings_;
    Posture current_ = Posture::Unknown;
    float reference_ = 0.0f;
    Posture candidate_ = Posture::Unknown;
    double since_ = 0.0;
    double last_ = 0.0;
    bool changing_ = false;
};

} // namespace evr::posture
