#pragma once

// Weapon mod switch by a wrist flick, Roll wrist in the launcher (docs/VR_INTERACTIONS.md section 2.7). On by
// default.
//
// A fast roll of the weapon hand about its pointing axis, the controller turned over and back like a
// wrist, presses the weapon mod switch (the game's `_reload`, the Y tap). The roll rate comes from the aim
// orientation between two frames: the rotation between them, as a rotation vector over the time between the
// frames' samples (InputFrame::sampleSeconds; the mapper's frame time when that is not known), split into the
// part about the pointing axis (the roll) and the rest (the pointing direction swinging round). The same
// sample read twice adds nothing, and a gap of kMaxRateGapSeconds or more (a load, a stall) reads no rate
// and starts the smoothing over.
//
// A flick is a roll rate (smoothed over a few frames) of `minRollDegrees` per second or more, in either
// direction, that is at least `dominance` times the rate at which the pointing direction swings, with the
// hand not travelling faster than `maxHandSpeed`. Fast aiming swings the pointing direction and carries the
// hand, so it is not a flick. One press per flick: nothing fires within `cooldownSeconds`; the next flick
// the same way waits until the roll has stayed below a quarter of the minimum for kRearmQuietSeconds, and a
// roll the other way, which is how a flick and back comes back, for kReturnQuietSeconds. A wrist turned over,
// held a moment and turned back is one flick; held still longer than kReturnQuietSeconds, the roll back is a
// flick of its own (the weapon mod switched back). A hand whose tracking is lost starts over, and a turn of
// more than 4000 degrees per second in one frame is a tracking jump, not a wrist, and reads nothing.
//
// Looking at the pointing axis: with the weapon pointing steeply up or down a turn of the body about the
// vertical is mostly a roll of the controller; the swing of the pointing direction it also has keeps that
// from being a flick unless it is very fast.

#include "common/quat.hpp"
#include "features/input/controller_state.hpp"

#include <optional>

namespace evr::input {

inline constexpr float kMinFlickDegreesPerSecond = 200.0f;
inline constexpr float kMaxFlickDegreesPerSecond = 1500.0f;
// A gap between two samples this long or longer is a stall, not a rotation rate worth reading.
inline constexpr float kMaxRateGapSeconds = 0.1f;
// How long the roll has to stay quiet (below a quarter of the minimum) before the next flick the same way,
// and before a flick the other way (the way the last flick's roll comes back).
inline constexpr float kRearmQuietSeconds = 0.2f;
inline constexpr float kReturnQuietSeconds = 0.8f;

struct FlickSettings {
    bool enabled = true;
    float minRollDegrees = 450.0f; // per second, about the pointing axis
    float dominance = 2.0f;        // the roll must be this many times the swing of the pointing direction
    float maxHandSpeed = 1.5f;     // metres per second
    float cooldownSeconds = 0.6f;
};

struct FlickOutput {
    bool flicked = false;
    // For tests and the log: the smoothed rates of this frame, degrees per second (roll signed).
    float rollDegrees = 0.0f;
    float swingDegrees = 0.0f;
    // For the log of a flick: the weapon's pointing pitch (degrees, up positive), the hand's speed (metres
    // per second) and the head's turn about the vertical (degrees per second, left positive; 0 without a head
    // pose). A body turned with the weapon pointing steeply down reads as a roll (see above): these tell that
    // from a wrist in a player's log.
    float pitchDegrees = 0.0f;
    float handSpeed = 0.0f;
    float headTurnDegrees = 0.0f;
};

class WristFlick {
public:
    // Values that are not finite or out of range fall back to the defaults.
    explicit WristFlick(FlickSettings settings = {});

    // `dtSeconds` is the time since the previous frame (the cooldown's clock, and the rate's when the frame
    // has no sample time). `blocked`: the game holds the gestures back (a cutscene, a forced view, the weapon
    // wheel up, ...): nothing fires and the flick starts over.
    FlickOutput update(const InputFrame& frame, Hand weaponHand, float dtSeconds, bool blocked = false);

    [[nodiscard]] const FlickSettings& settings() const { return settings_; }

private:
    void reset();

    FlickSettings settings_;
    std::optional<Quat> previous_;
    std::optional<float> previousHeadYaw_; // radians
    double previousSample_ = 0.0;          // the previous frame's sample time (0: not known)
    float roll_ = 0.0f;                    // smoothed, degrees per second
    float swing_ = 0.0f;                   // smoothed, degrees per second
    bool armed_ = true;                    // quiet for kRearmQuietSeconds since the last flick
    bool armedReturn_ = true;              // quiet for kReturnQuietSeconds since the last flick
    float quiet_ = 0.0f;                   // how long the roll has been quiet
    float lastSign_ = 0.0f;                // the direction of the last flick's roll (0: none since the start)
    float cooldown_ = 0.0f;
    Hand weaponHand_ = Hand::Right;
};

} // namespace evr::input
