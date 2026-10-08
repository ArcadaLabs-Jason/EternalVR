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

// Head heights above the floor a player can have: a head low over the floor (sitting on it, a deep
// crouch) is still above 0.5 m, and the eyes of a 2.2 m tall player are below 2.3 m. A reading outside
// them is a floor the runtime got wrong (SteamVR once put its floor at head height after a recenter: the
// head 0.06 m above it), and it never decides the posture.
constexpr float kMinHeadAboveFloorMetres = 0.5f;
constexpr float kMaxHeadAboveFloorMetres = 2.3f;

// The height is finite and within the range above.
bool plausibleHeadHeight(float headAboveFloorMetres);

class PostureDetector {
public:
    // Thresholds that are not finite, lie outside 0-3 m or are not ordered (seated below standing)
    // fall back to the defaults as a pair.
    explicit PostureDetector(PostureThresholds thresholds = {});

    // Feeds one head-height sample and returns the detected posture.
    //   - Below the seated threshold: Seated. Above the standing threshold: Standing.
    //   - Between them: keep the previous posture. With no previous posture, split the band at its
    //     midpoint so the very first sample still gives an answer.
    //   - Unknown or non-finite height (no floor-relative space, tracking lost, a glitched sample) or one
    //     no head can have (plausibleHeadHeight): keep the previous posture, which stays Unknown until a
    //     plausible height has been seen.
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

// The detected posture after a full anchor.
struct AnchorPosture {
    Posture posture = Posture::Unknown;
    bool detected = false; // decided from this anchor's height (else the posture in force, kept)
};

// `detect`: the first anchor of the session or the player's own recenter, which detect the posture again
// from `headAboveFloor` (`detector` is reset first). Otherwise (the runtime's recenter, which can move the
// floor too) `inForce` stays, unless it is Unknown: then a usable height detects it. No height, or one no
// head can have, keeps `inForce` as well (Unknown at the first anchor, as without a floor space), so a
// broken floor never changes the posture.
AnchorPosture
postureAtAnchor(PostureDetector& detector, Posture inForce, std::optional<float> headAboveFloor, bool detect);

} // namespace evr::posture
