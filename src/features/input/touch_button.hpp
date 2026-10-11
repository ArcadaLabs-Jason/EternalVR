#pragma once

// A thumb rest bound like a button (docs/VR_CONTROLLERS.md, "A thumb rest as a button"; DECISIONS T-125).
//
// The rest is a touch sensor, not a switch, so it is read the way the thumb-rest wheel reads it: down from
// the touch's landing to its lift as rest_touch_filter.hpp filters them (a flicker is one touch, a touch
// begun with the thumb on its stick or buttons waits, a lift with the thumb gone to them is at once). The
// press kinds:
// - press: down from the landing to the lift. After a plain lift that is up to a hold-over after the thumb
//   left; after a lift cut short (the thumb went to a button or its stick) at once.
// - tap: one frame, on the lift of a touch that did not become a hold, so a touch is a tap or a hold, never
//   neither: the touch as sensed (from the landing to when it last went) was at most about the hold time.
//   Not the buttons' TapHoldDetector on the press: the hold-over pads every touch (a 0.15 s touch lifts
//   0.30 s after its landing), which would make every real tap a hold. A lift cut short (the thumb went to a
//   button or its stick) after a touch sensed for less than kRestDebounceSeconds is no tap: the thumb passed
//   over the rest on its way.
// - hold: from the touch having lasted the hold time, on a frame the sensor reports it (it waits through a
//   gap), until the lift.
// A landing within kOwnButtonVoidSeconds of the hand's face button going up, or within kOwnStickVoidSeconds
// of its own stick being out of the centre, is the thumb coming off its own controls (a jump, a turn): it is
// no press of any kind until the thumb lifts and lands again.
//
// Pure, no clock of its own: the mapper feeds it once per frame (input_mapper.hpp).

#include "features/input/rest_touch_filter.hpp"
#include "features/input/tap_hold.hpp"

#include <cstdint>

namespace evr::input {

struct TouchButtonSample {
    bool raw = false;        // the thumb rest's touch
    bool vetoTouch = false;  // a touch sensor beside the rest: the thumb on its stick or face buttons
    bool faceButton = false; // the hand's primary or secondary button pressed
    bool stickOut = false;   // the hand's own stick out of the centre
};

struct TouchButtonOutput {
    bool down = false; // press
    bool tap = false;
    bool hold = false;
};

// Counted from the start for the log (vkcore/rest_wheel.cpp prints the change every 10 s).
struct TouchButtonStats {
    std::uint32_t touches = 0;        // the sensor reported a touch
    std::uint32_t shortTouches = 0;   // ... gone again within kRestDebounceSeconds
    std::uint32_t begunElsewhere = 0; // ... begun with the thumb elsewhere: held back
    std::uint32_t gapsForgiven = 0;   // a registered touch lost under the hold-over
    std::uint32_t cutLifts = 0;       // lifts made at once: the thumb went elsewhere
    std::uint32_t passedOver = 0;     // ... of those, a touch sensed under kRestDebounceSeconds: no tap
    std::uint32_t taps = 0;
    std::uint32_t holds = 0;
    std::uint32_t voided = 0; // landings straight off the hand's own controls
};

class TouchButton {
public:
    TouchButton() = default;
    // A hold time outside 0..kMaxHoldSeconds, or a hold-over outside the rest's range, falls back to its
    // default.
    TouchButton(float holdSeconds, float holdOverSeconds);

    // `dtSeconds`: the frame's seconds; non-finite or negative counts as zero.
    TouchButtonOutput update(const TouchButtonSample& sample, float dtSeconds);

    // The touch going on (or one whose contact has begun) is used up by something else (a menu's hold
    // ended): it presses nothing until it lifts.
    void cancel();

    // The touch is registered (down, unless it is used up).
    [[nodiscard]] bool isDown() const { return filter_.touched(); }
    [[nodiscard]] float holdSeconds() const { return holdSeconds_; }
    [[nodiscard]] const TouchButtonStats& stats() const { return stats_; }

private:
    static constexpr float kLongAgoSeconds = 1000.0f;

    RestTouchFilter filter_;
    float holdSeconds_ = kDefaultHoldSeconds;
    bool voided_ = false;    // this landing presses nothing
    bool cancelled_ = false; // ... nor the one the contact going on makes
    bool holding_ = false;
    float sinceLanding_ = 0.0f;
    float sinceFaceButton_ = kLongAgoSeconds;
    float centredSeconds_ = kLongAgoSeconds;
    TouchButtonStats stats_;
};

} // namespace evr::input
