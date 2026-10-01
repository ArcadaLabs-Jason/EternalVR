#pragma once

// Landing from a fall (a tester's idea, public issue #1): the height of the player's feet, read once a game
// frame, tells when the body leaves the ground and when it is back on it. A landing is a fall that stops: the
// feet going down faster than kAirborneSpeed, then at rest (the top of a jump never follows a fall, and a
// double jump turns the fall back into a rise). It reports the drop from the highest point of the time in the
// air and the fastest fall, so the caller can leave ordinary jumps out (a jump lands its own height below its
// peak) and scale the effect by the drop. Heights are game units, about a metre each. Pure.

#include <optional>

namespace evr::bhaptics {

struct Landing {
    float drop = 0.0f;      // from the highest point in the air down to where the feet came to rest
    float fallSpeed = 0.0f; // the fastest fall on the way down, units per second
};

// Up or down faster than this: off the ground (walking down stairs and slopes stays below it).
inline constexpr float kAirborneSpeed = 1.5f;
// Slower than this after a fall: at rest again.
inline constexpr float kRestSpeed = 0.5f;
// In the air without a fall and this long at rest (onto a ledge, a lift): back on the ground quietly.
inline constexpr double kQuietRestSeconds = 0.3;
// Faster than this is a teleport or a respawn, not a fall.
inline constexpr float kTeleportSpeed = 60.0f;
// Readings further apart than this (a load, the game stalled) start over.
inline constexpr double kMaxReadingGapSeconds = 0.5;

class LandingDetector {
public:
    // One reading of the feet's height at `seconds`, the time the game frame was read (a repeat of the same
    // reading changes nothing). A landing when the feet just came to rest after falling.
    std::optional<Landing> update(float height, double seconds);

    void reset();

    [[nodiscard]] bool airborne() const { return airborne_; }

private:
    std::optional<float> lastHeight_;
    double lastSeconds_ = 0.0;
    bool airborne_ = false;
    bool falling_ = false;
    float peak_ = 0.0f;
    float fastestFall_ = 0.0f;
    std::optional<double> restSince_;
};

} // namespace evr::bhaptics
