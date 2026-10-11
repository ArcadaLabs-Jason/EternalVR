#include "features/input/touch_button.hpp"

#include <cmath>

namespace evr::input {

TouchButton::TouchButton(float holdSeconds, float holdOverSeconds)
    : filter_(holdOverSeconds), holdSeconds_(TapHoldDetector(holdSeconds).holdSeconds()) {}

TouchButtonOutput TouchButton::update(const TouchButtonSample& sample, float dtSeconds) {
    const float dt = std::isfinite(dtSeconds) && dtSeconds > 0.0f ? dtSeconds : 0.0f;
    sinceFaceButton_ = sample.faceButton ? 0.0f : std::fmin(sinceFaceButton_ + dt, kLongAgoSeconds);
    centredSeconds_ = sample.stickOut ? 0.0f : std::fmin(centredSeconds_ + dt, kLongAgoSeconds);
    const bool wasTouched = filter_.touched();
    // A face button pressed or its own stick out is hard evidence of the thumb elsewhere.
    const RestTouchStep step =
        filter_.update({sample.raw, sample.vetoTouch, sample.faceButton || sample.stickOut}, dt);
    stats_.touches += step.rawTouch ? 1 : 0;
    stats_.shortTouches += step.shortTouch ? 1 : 0;
    stats_.begunElsewhere += step.vetoedLanding ? 1 : 0;
    stats_.gapsForgiven += step.bridged ? 1 : 0;
    stats_.cutLifts += step.cutLift ? 1 : 0;

    TouchButtonOutput out;
    if (step.landed) {
        sinceLanding_ = 0.0f;
        holding_ = false;
        const bool ownControls =
            sinceFaceButton_ < kOwnButtonVoidSeconds || centredSeconds_ < kOwnStickVoidSeconds;
        stats_.voided += ownControls ? 1 : 0;
        voided_ = ownControls || cancelled_;
        cancelled_ = false;
    } else if (wasTouched) {
        sinceLanding_ += dt;
    }
    if (step.lifted) {
        // The touch as sensed: from the landing to when it last went, the hold-over not counted. A hold fires
        // on the first frame it is sensed past the hold time, so a touch that did not become a hold is a tap
        // (comparing this length with the hold time instead leaves a frame's width that is neither).
        const bool passedOver = step.cutLift && sinceLanding_ - filter_.rawSeconds() < kRestDebounceSeconds;
        stats_.passedOver += passedOver && !voided_ ? 1 : 0;
        out.tap = !voided_ && !holding_ && !passedOver;
        stats_.taps += out.tap ? 1 : 0;
        voided_ = false;
        holding_ = false;
        return out;
    }
    if (!filter_.touched()) {
        // A contact that lapses without registering takes its cancel with it.
        cancelled_ = cancelled_ && filter_.contact();
        return out;
    }
    if (voided_) {
        return out;
    }
    if (!holding_ && filter_.raw() && sinceLanding_ >= holdSeconds_) {
        holding_ = true;
        ++stats_.holds;
    }
    out.down = true;
    out.hold = holding_;
    return out;
}

void TouchButton::cancel() {
    if (filter_.touched()) {
        voided_ = true;
    } else {
        cancelled_ = filter_.contact();
    }
}

} // namespace evr::input
