#pragma once

// Weapon mod switch by a wrist flick, Roll wrist in the launcher (docs/VR_INTERACTIONS.md section 2.7). On by
// default.
//
// The weapon hand turned over and back, fast, about its pointing axis like a wrist, presses the weapon mod
// switch (the game's `_reload`, the Y tap). The roll rate comes from the aim orientation between two frames:
// the rotation between them, as a rotation vector over the time between the frames' samples
// (InputFrame::sampleSeconds; the mapper's frame time when that is not known), split into the part about the
// pointing axis (the roll) and the rest (the pointing direction swinging round). The same sample read twice
// adds nothing, and a gap of kMaxRateGapSeconds or more (a load, a stall) reads no rate and starts over.
//
// The roll rate (smoothed over a few frames) also adds up to the angle the hand has turned, and its turning
// points split that into strokes, one way then the other. A flick is two strokes:
// - the over: kFlickOverDegrees or more one way, its roll rate reaching `minRollDegrees` per second at least
//   `dominance` times the rate at which the pointing direction swings, with the hand not travelling faster
//   than `maxHandSpeed`, in the same frame. Fast aiming swings the pointing direction and carries the hand.
// - the back: the other way, its rate reaching the minimum and at least the swing's, and kFlickBackDegrees
//   back, both within kFlickBackSeconds of the over's furthest point. The flick fires there.
// A stroke shorter than kMinStrokeDegrees (a wobble at the turn, a hesitation in the middle of a roll) is no
// stroke: the one before it goes on. The over's furthest point moves on for its timer only when the hand goes
// kHoldTremorDegrees further, so a hand turned over and held with a tremor is still held, and its back comes
// too late.
// A roll one way only, however fast, is no flick: a forearm turning one way as the player turned in the room
// switched the weapon mod in a player's session, and most rolls taken for a flick in recorded play went one
// way (DECISIONS T-202). One press per flick: nothing fires within `cooldownSeconds`, nor until the roll has
// stayed below a quarter of the minimum for kRearmQuietSeconds. A hand whose tracking is lost starts over,
// and a turn of more than 4000 degrees per second in one frame is a tracking jump, not a wrist, and reads
// nothing.

#include "common/quat.hpp"
#include "features/input/controller_state.hpp"

#include <optional>

namespace evr::input {

inline constexpr float kMinFlickDegreesPerSecond = 200.0f;
inline constexpr float kMaxFlickDegreesPerSecond = 1500.0f;
// A gap between two samples this long or longer is a stall, not a rotation rate worth reading.
inline constexpr float kMaxRateGapSeconds = 0.1f;
// How long the roll has to stay quiet (below a quarter of the minimum) before the next flick.
inline constexpr float kRearmQuietSeconds = 0.2f;
// The flick's two strokes: how far the hand turns over and how far back, in degrees, and how soon after the
// over's furthest point the back must have come that far. Deliberate flicks in a headset turned over 130 to
// 160 degrees and came back 70 to 140 within about 0.1 s; of the 129 rolls in six hours of recorded play that
// the one-stroke flick up to 0.1.87 took for a flick, 9 in 10 came back less than 60 degrees, and these
// strokes fire on none of them (docs/VR_INTERACTIONS.md section 2.7).
inline constexpr float kFlickOverDegrees = 90.0f;
inline constexpr float kFlickBackDegrees = 60.0f;
inline constexpr float kFlickBackSeconds = 0.3f;
// A stroke turns once the angle has come back this far from its furthest point; a stroke shorter than
// kMinStrokeDegrees is a wobble in the stroke before it. A held hand's tremor is a degree or two.
inline constexpr float kStrokeDegrees = 10.0f;
inline constexpr float kMinStrokeDegrees = 2.0f * kStrokeDegrees;
inline constexpr float kHoldTremorDegrees = 2.0f;

struct FlickSettings {
    bool enabled = true;
    float minRollDegrees = 450.0f; // per second, about the pointing axis
    float dominance = 2.0f;    // the over's roll must be this many times the swing of the pointing direction
    float maxHandSpeed = 1.5f; // metres per second, during the over
    float cooldownSeconds = 0.6f;
};

struct FlickOutput {
    bool flicked = false;
    // For tests and the log: the smoothed rates of this frame, degrees per second (roll signed).
    float rollDegrees = 0.0f;
    float swingDegrees = 0.0f;
    // For the log of a flick: how far the hand turned over and how far back when it fired (degrees), the
    // weapon's pointing pitch (degrees, up positive), the hand's speed (metres per second) and the head's
    // turn about the vertical (degrees per second, left positive; 0 without a head pose).
    float overDegrees = 0.0f;
    float backDegrees = 0.0f;
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
    // One stroke on the angle turned about the pointing axis (degrees; times on the strokes' clock).
    struct Stroke {
        float start = 0.0f;     // where it began (the last turning point)
        float end = 0.0f;       // how far it has gone
        float heldEnd = 0.0f;   // the furthest point its timer counts from (kHoldTremorDegrees steps)
        float endTime = 0.0f;   // when it got there
        float direction = 0.0f; // +1 or -1 once it has gone kStrokeDegrees, 0 before
        bool backFast = false;  // its rate reached the minimum and the swing's (the back's test)
        bool overFast = false;  // ... and twice the swing with a still hand (the over's test)
    };
    // What the stroke before the current one left: whether it was an over, so its back may fire.
    struct Over {
        bool done = false;
        float degrees = 0.0f;
        float endTime = 0.0f; // its furthest point (held)
    };

    void reset();
    void restartStrokes();
    // The strokes after this frame's roll (`roll`, degrees per second unsmoothed, over `step` seconds); true
    // when the flick fires.
    bool strokes(float roll, float step, bool still, FlickOutput& out);
    void turnStroke();

    FlickSettings settings_;
    std::optional<Quat> previous_;
    std::optional<float> previousHeadYaw_; // radians
    double previousSample_ = 0.0;          // the previous frame's sample time (0: not known)
    float roll_ = 0.0f;                    // smoothed, degrees per second
    float swing_ = 0.0f;                   // smoothed, degrees per second
    bool armed_ = true;                    // quiet for kRearmQuietSeconds since the last flick
    float quiet_ = 0.0f;                   // how long the roll has been quiet
    float cooldown_ = 0.0f;
    Hand weaponHand_ = Hand::Right;
    float clock_ = 0.0f; // seconds since the strokes started over
    float angle_ = 0.0f; // degrees turned about the pointing axis since then
    Stroke stroke_;
    Over over_;
    // The stroke before this one and the over before that, to go back to when this one is a wobble.
    std::optional<Stroke> previousStroke_;
    Over previousOver_;
};

} // namespace evr::input
