#include "features/input/rest_touch_filter.hpp"

#include <algorithm>
#include <cmath>

namespace evr::input {

RestTouchStep RestTouchFilter::update(const RestTouchSample& sample, float dt) {
    RestTouchStep step;
    const bool raw = sample.raw;
    // The thumb elsewhere: hard evidence (a face button pressed, or its own stick out of the centre, the
    // thumb on it, on a runtime that reports no stick touch too), or a touch sensor beside the rest.
    const bool hard = sample.hard;
    const bool vetoTouch = sample.vetoTouch;
    vetoSeconds_ = vetoTouch && vetoTouch_ ? vetoSeconds_ + dt : 0.0f;
    vetoToggled_ = vetoTouch && vetoToggled_;
    vetoTouch_ = vetoTouch;
    // A touch sensor on all along while the rest comes and goes sees the resting thumb itself.
    const bool stuck = vetoTouch && vetoToggled_ && vetoSeconds_ > kStuckVetoSeconds;
    const bool veto = hard || (vetoTouch && !stuck);
    step.veto = veto;
    // A touch sensor cuts a hold-over only once steady (one flickering with the rest cuts nothing), and not
    // when stuck (a press then still cuts).
    const bool elsewhere = hard || (vetoTouch && !stuck && vetoSeconds_ >= kVetoTouchSteadySeconds);
    const bool wentElsewhere = elsewhere && !elsewhere_;
    elsewhere_ = elsewhere;
    // The thumb moved to a button or the stick: a lost touch lifts at once.
    bool cut = false;
    if (raw != raw_) {
        if (raw) {
            step.rawTouch = true;
            vetoToggled_ = vetoTouch;
            if (touched_) {
                step.bridged = true; // back within the hold-over: forgiven
                step.lostSeconds = rawSeconds_;
            } else if (!contact_ && veto) {
                step.vetoedLanding = true;
            }
        } else {
            step.shortTouch = rawSeconds_ < kRestDebounceSeconds;
            cut = touched_ && elsewhere && wentElsewhere_;
        }
        raw_ = raw;
        rawSeconds_ = 0.0f;
        if (!raw && touched_ && !gap_) {
            gap_ = true;
            gapSeconds_ = 0.0f;
        } else if (gap_) {
            gapSeconds_ += dt;
        }
    } else {
        rawSeconds_ += dt;
        if (gap_) {
            gapSeconds_ += dt;
            gap_ = !(raw_ && rawSeconds_ >= kRestDebounceSeconds);
        }
    }
    if (raw_) {
        contactSeconds_ = contact_ ? contactSeconds_ + dt : 0.0f;
        contact_ = true;
    } else if (contact_) {
        contactSeconds_ += dt;
        contact_ = rawSeconds_ < holdOverSeconds_;
    }
    if (touched_ && wentElsewhere) {
        wentElsewhere_ = true;
        cut = cut || !raw_;
    }
    const float holdOver = std::clamp(onSeconds_, kRestHoldOverFloorSeconds, holdOverSeconds_);
    if (!touched_ && raw_ && (!veto || contactSeconds_ >= kRestDebounceSeconds)) {
        step.stuckVeto = !hard && stuck && contactSeconds_ < kRestDebounceSeconds;
        touched_ = true;
        wentElsewhere_ = false;
        onSeconds_ = 0.0f;
        gap_ = false;
        step.landed = true;
    } else if (touched_ && !raw_ && (cut || rawSeconds_ >= holdOver)) {
        touched_ = false;
        wentElsewhere_ = false;
        contact_ = false;
        gap_ = false; // gapSeconds stays: the wheel's cancel reads it
        step.lifted = true;
        step.cutLift = cut;
        step.hardLift = cut && hard;
        // Quick returns count from when the touch went, not from the end of the hold-over.
        sinceLift_ = cut ? 0.0f : rawSeconds_;
    } else if (touched_ && raw_) {
        onSeconds_ += dt;
    }
    if (!touched_ && !step.lifted) {
        sinceLift_ = std::fmin(sinceLift_ + dt, kLongAgoSeconds);
    }
    return step;
}

void RestTouchFilter::land() {
    touched_ = true;
    wentElsewhere_ = false;
    onSeconds_ = 0.0f;
    gap_ = false;
}

} // namespace evr::input
