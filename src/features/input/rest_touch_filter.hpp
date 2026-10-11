#pragma once

// One thumb rest's touch, filtered (DECISIONS T-120; some sensors flicker on and off very fast): when a thumb
// lands on the rest and when it lifts. The thumb-rest wheel (rest_wheel.hpp) reads each rest through it.
//
// - Landing: a touch registers on its first frame, unless the thumb is elsewhere on that controller then: a
//   face button pressed or its own stick out of the centre (hard evidence), or a touch sensor beside the rest
//   (the stick's, the face buttons' where they are not the rest, the trackpad's; the mapper reads them). Then
//   it registers once the thumb is no longer elsewhere, or once the contact (the touch with gaps shorter than
//   the hold-over bridged) has lasted kRestDebounceSeconds. A touch sensor on for kStuckVetoSeconds while the
//   rest keeps coming and going sees the resting thumb itself: it no longer holds a landing back.
// - Lift: a touch lost for less than the hold-over is a forgiven gap. The hold-over is
//   ETERNALVR_THUMBREST_HOLDOVER (kRestHoldOverSeconds by default), but never longer than the touch has been
//   on since its landing, nor shorter than kRestHoldOverFloorSeconds, so a brush lifts soon. A longer loss
//   lifts the thumb; at once when the thumb went elsewhere after the landing: hard evidence at once, a touch
//   sensor once it has been on kVetoTouchSteadySeconds (one flickering with the rest cuts nothing). The thumb
//   going elsewhere while the touch is still sensed changes nothing.
//
// Pure, no clock of its own: the owner feeds it once per frame.

#include "common/finite.hpp"

namespace evr::input {

// A touch begun with the thumb elsewhere registers once it has lasted this long.
inline constexpr float kRestDebounceSeconds = 0.06f;
// A touch lost for less than this is a gap, not a lift (the default of ETERNALVR_THUMBREST_HOLDOVER).
inline constexpr float kRestHoldOverSeconds = 0.15f;
inline constexpr float kMinRestHoldOverSeconds = 0.10f;
inline constexpr float kMaxRestHoldOverSeconds = 0.30f;
// The hold-over is never shorter than this, however briefly the touch was on.
inline constexpr float kRestHoldOverFloorSeconds = 0.05f;
// A touch sensor beside the rest cuts a hold-over short only once it has been on this long.
inline constexpr float kVetoTouchSteadySeconds = 0.04f;
// A touch sensor on this long while the rest comes and goes no longer holds a landing back.
inline constexpr float kStuckVetoSeconds = 1.5f;
// A landing this soon after the hand's own face button went up, or its own stick was out of the centre, comes
// from the player's own controls: under edge it opens no window (rest_wheel.hpp), and a rest bound like a
// button presses nothing (touch_button.hpp).
inline constexpr float kOwnButtonVoidSeconds = 0.4f;
inline constexpr float kOwnStickVoidSeconds = 0.4f;

// One frame of what a rest's controller reports.
struct RestTouchSample {
    bool raw = false;       // the rest's touch sensor
    bool vetoTouch = false; // a touch sensor beside the rest: the thumb elsewhere
    bool hard = false;      // the hand's face button pressed, or its own stick out of the centre
};

// What the touch did this frame.
struct RestTouchStep {
    bool landed = false;
    bool lifted = false;
    bool cutLift = false;  // ... at once: the thumb went elsewhere after the landing
    bool hardLift = false; // ... on hard evidence (a press, its own stick out)
    // The touch came back within the hold-over (a forgiven gap), after `lostSeconds` without it.
    bool bridged = false;
    float lostSeconds = 0.0f;
    // The thumb is elsewhere this frame: a touch begun now waits.
    bool veto = false;
    // Counted for the log.
    bool rawTouch = false;      // the sensor reported a touch
    bool shortTouch = false;    // ... gone again within kRestDebounceSeconds (flutter)
    bool vetoedLanding = false; // ... begun with the thumb elsewhere: held back
    bool stuckVeto = false;     // a landing a stuck touch sensor no longer held back
};

class RestTouchFilter {
public:
    RestTouchFilter() = default;
    // A hold-over outside kMinRestHoldOverSeconds..kMaxRestHoldOverSeconds (or not finite) falls back to the
    // default.
    explicit RestTouchFilter(float holdOverSeconds)
        : holdOverSeconds_(finiteInRangeOr(
              holdOverSeconds, kMinRestHoldOverSeconds, kMaxRestHoldOverSeconds, kRestHoldOverSeconds)) {}

    // `dt`: the frame's seconds, finite and not negative.
    RestTouchStep update(const RestTouchSample& sample, float dt);

    // The touch, registered, lands afresh on this frame (the wheel's taps of the rest under edge): the
    // owner's own landing, after update().
    void land();

    [[nodiscard]] bool touched() const { return touched_; } // registered
    [[nodiscard]] bool raw() const { return raw_; }
    // The contact: the touch with gaps shorter than the hold-over bridged, and since its first frame.
    [[nodiscard]] bool contact() const { return contact_; }
    [[nodiscard]] float contactSeconds() const { return contactSeconds_; }
    // The sensor on since the landing (gaps not counted).
    [[nodiscard]] float onSeconds() const { return onSeconds_; }
    // Since the sensor last changed.
    [[nodiscard]] float rawSeconds() const { return rawSeconds_; }
    // Since the touch first went in the last loss (a flicker back that is forgiven does not restart it).
    [[nodiscard]] float gapSeconds() const { return gapSeconds_; }
    // Since the touch was lifted (from when it went), long ago at the start.
    [[nodiscard]] float sinceLift() const { return sinceLift_; }
    [[nodiscard]] float holdOverSeconds() const { return holdOverSeconds_; }

private:
    static constexpr float kLongAgoSeconds = 1000.0f;

    float holdOverSeconds_ = kRestHoldOverSeconds;
    bool raw_ = false;
    bool touched_ = false;
    bool elsewhere_ = false;     // evidence that cuts a hold-over short, last frame
    bool wentElsewhere_ = false; // ... and that began after the landing
    bool vetoTouch_ = false;     // a touch sensor beside the rest, last frame
    bool vetoToggled_ = false;   // ... and the rest came on again while it was
    float vetoSeconds_ = 0.0f;   // ... on without a break
    float rawSeconds_ = 0.0f;
    // The loss is over once the touch is back for the debounce, or the rest lands or lifts.
    bool gap_ = false;
    float gapSeconds_ = 0.0f;
    bool contact_ = false;
    float contactSeconds_ = 0.0f;
    float onSeconds_ = 0.0f;
    float sinceLift_ = kLongAgoSeconds;
};

} // namespace evr::input
