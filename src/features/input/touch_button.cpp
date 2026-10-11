#include "features/input/touch_button.hpp"

#include <cmath>

namespace evr::input {

TouchButton::TouchButton(float holdSeconds, float holdOverSeconds, TouchKind kind)
    : filter_(holdOverSeconds), kind_(kind), holdSeconds_(TapHoldDetector(holdSeconds).holdSeconds()) {}

bool TouchButton::landingVoid(const TouchButtonSample& sample) const {
    if (kind_ == TouchKind::Control) {
        // The control under the touch in use now, or a moment ago: the thumb or finger is working that
        // control.
        return sample.ownLanding || sinceOwnLanding_ < kOwnButtonVoidSeconds;
    }
    return sinceFaceButton_ < kOwnButtonVoidSeconds || centredSeconds_ < kOwnStickVoidSeconds;
}

bool TouchButton::liftTaps(const RestTouchStep& step) {
    // The touch as sensed: from the landing to when it last went, the hold-over not counted. A hold fires
    // on the first frame it is sensed past the hold time, so a touch that did not become a hold is a tap
    // (comparing this length with the hold time instead leaves a frame's width that is neither).
    const bool brief = sinceLanding_ - filter_.rawSeconds() < kRestDebounceSeconds;
    if (kind_ == TouchKind::Control) {
        // A brush on the way past.
        stats_.brushes += brief && !voided_ && !holding_ ? 1 : 0;
        return !voided_ && !holding_ && !brief;
    }
    // A lift cut short after a brief touch: the thumb passed over the rest on its way to its controls.
    const bool passedOver = step.cutLift && brief;
    stats_.passedOver += passedOver && !voided_ ? 1 : 0;
    return !voided_ && !holding_ && !passedOver;
}

TouchButtonOutput TouchButton::update(const TouchButtonSample& sample, float dtSeconds) {
    const float dt = std::isfinite(dtSeconds) && dtSeconds > 0.0f ? dtSeconds : 0.0f;
    const bool rest = kind_ == TouchKind::Rest;
    sinceFaceButton_ = sample.faceButton ? 0.0f : std::fmin(sinceFaceButton_ + dt, kLongAgoSeconds);
    centredSeconds_ = sample.stickOut ? 0.0f : std::fmin(centredSeconds_ + dt, kLongAgoSeconds);
    const bool wasTouched = filter_.touched();
    // On a rest a face button pressed or its own stick out is hard evidence of the thumb elsewhere; a touch
    // on a control has nothing beside it (its own control is read below).
    const RestTouchSample filtered =
        rest ? RestTouchSample{sample.raw, sample.vetoTouch, sample.faceButton || sample.stickOut}
             : RestTouchSample{sample.raw, false, false};
    const RestTouchStep step = filter_.update(filtered, dt);
    stats_.touches += step.rawTouch ? 1 : 0;
    stats_.shortTouches += step.shortTouch ? 1 : 0;
    stats_.begunElsewhere += step.vetoedLanding ? 1 : 0;
    stats_.gapsForgiven += step.bridged ? 1 : 0;
    stats_.cutLifts += step.cutLift ? 1 : 0;

    TouchButtonOutput out;
    if (step.landed) {
        sinceLanding_ = 0.0f;
        holding_ = false;
        const bool ownControls = landingVoid(sample);
        stats_.voided += ownControls ? 1 : 0;
        voided_ = ownControls || cancelled_;
        cancelled_ = false;
    } else if (wasTouched) {
        sinceLanding_ += dt;
    }
    // A touch on a control that goes on to use it: no press from here, and no tap.
    if (!rest && sample.ownControl && (filter_.touched() || step.lifted) && !voided_) {
        voided_ = true;
        ++stats_.ownUse;
    }
    sinceOwnLanding_ = sample.ownLanding ? 0.0f : std::fmin(sinceOwnLanding_ + dt, kLongAgoSeconds);
    if (step.lifted) {
        out.tap = liftTaps(step);
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
