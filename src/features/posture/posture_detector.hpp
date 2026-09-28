#pragma once

// Automatic seated/standing detection (ARCHITECTURE sections 6 and 12).
//
// Posture only adjusts posture-dependent defaults; the view itself is anchored to eye height either
// way (see eye_height.hpp). Detection uses the head's height above the floor, which comes from the
// LOCAL_FLOOR or STAGE space when the runtime has one and is unknown otherwise.

#include <cstdint>
#include <optional>

namespace evr::posture {

enum class Posture : std::uint8_t {
    Unknown,
    Seated,
    Standing,
};

enum class PostureOverride : std::uint8_t {
    Auto,
    Seated,
    Standing,
};

// Head heights (metres above the floor) at which the detected posture switches. The gap between the
// two is hysteresis: a seated player leaning up or a standing player crouching briefly should not
// flip posture back and forth. Starting values, to be tuned in playtests:
//   - Seated head heights cluster around 1.1-1.25 m for adults on a chair.
//   - Standing head heights start around 1.5 m for short adults.
struct PostureThresholds {
    float seatedBelowMetres = 1.30f;
    float standingAboveMetres = 1.45f;
};

class PostureDetector {
public:
    // Thresholds that are not finite, lie outside 0-3 m or are not ordered (seated below standing)
    // fall back to the defaults as a pair.
    explicit PostureDetector(PostureThresholds thresholds = {});

    // Feeds one head-height sample and returns the detected posture.
    //   - Below the seated threshold: Seated. Above the standing threshold: Standing.
    //   - Between them: keep the previous posture. With no previous posture, split the band at its
    //     midpoint so the very first sample still gives an answer.
    //   - Unknown or non-finite height (no floor-relative space, tracking lost, a glitched sample):
    //     keep the previous posture, which stays Unknown until a height has been seen.
    Posture update(std::optional<float> headHeightAboveFloor);

    [[nodiscard]] Posture current() const { return current_; }
    [[nodiscard]] const PostureThresholds& thresholds() const { return thresholds_; }

    // Forgets the detected posture, e.g. when the reference space changes.
    void reset() { current_ = Posture::Unknown; }

private:
    PostureThresholds thresholds_;
    Posture current_ = Posture::Unknown;
};

// The posture the rest of the mod should use: the player's override if set, else the detected one.
Posture effectivePosture(PostureOverride playerOverride, Posture detected);

} // namespace evr::posture
