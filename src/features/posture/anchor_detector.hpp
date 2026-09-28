#pragma once

// The first stable head pose (T-029, T-045, T-106): when the automatic height anchor may be taken.
//
// The anchor maps the head's height at that moment to the character's eye height, so it must be taken
// from a head that is worn and settled, never from a headset lying on a desk or one still being put on.
// A sample counts when the session is focused, the head is tracked and, where the runtime reports it,
// the user is present. Over a window of counted samples the head must be:
//   - stable: its position spans less than `stableMetres` (T-045: under 2 cm over 1 s), and
//   - worn: its position spans more than `wornMetres` or its orientation more than `wornRadians`. A worn
//     headset always shows small head motion; a headset on a desk shows only tracking noise, below both.
// Any sample that does not count clears the window, so the whole window is one uninterrupted stretch.
// There is no file or OpenXR access here; the presenter feeds the samples.

#include "common/pose.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace evr::posture {

struct AnchorThresholds {
    float windowSeconds = 1.0f;
    float stableMetres = 0.02f;
    float wornMetres = 0.001f;
    float wornRadians = 0.0035f; // 0.2 degrees
};

struct HeadSample {
    double seconds = 0.0;            // monotonic time
    Pose pose;                       // the head in the tracking space
    bool tracked = false;            // orientation and position valid
    bool focused = false;            // the session is focused
    std::optional<bool> userPresent; // nullopt: the runtime does not report presence
};

class AnchorDetector {
public:
    // Thresholds that are not finite and positive (or a stable span not above the worn span) fall back to
    // the defaults as a set.
    explicit AnchorDetector(AnchorThresholds thresholds = {});

    // Feeds one sample. Returns the anchor pose (the newest sample's) on the sample that completes a
    // stable, worn window, once; afterwards nullopt until reset().
    std::optional<Pose> update(const HeadSample& sample);

    [[nodiscard]] bool anchored() const { return anchored_; }
    [[nodiscard]] const AnchorThresholds& thresholds() const { return thresholds_; }

    // Why the last sample did not anchor (for the log): counts of samples in the window and the spans.
    struct Window {
        std::size_t samples = 0;
        double seconds = 0.0;
        float positionSpan = 0.0f;
        float rotationSpan = 0.0f;
    };
    [[nodiscard]] Window window() const;

    // Forgets the window and the anchor, so the next stable, worn window anchors again.
    void reset();

private:
    AnchorThresholds thresholds_;
    std::vector<HeadSample> samples_; // oldest first, at most one window long
    bool anchored_ = false;
};

} // namespace evr::posture
