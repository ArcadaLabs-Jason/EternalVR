// The thumb-rest wheel's touch handling (rest_wheel.hpp): each rest's landing and lift through its filter
// (rest_touch_filter.hpp), a rest tapped again under edge, and the other stick held while a touch decides.

#include "features/input/rest_wheel.hpp"

#include <array>
#include <cstddef>
#include <optional>

namespace evr::input {

namespace {

std::size_t index(Hand hand) {
    return static_cast<std::size_t>(hand);
}

} // namespace

void RestWheel::debounce(const RestWheelFrame& frame,
                         float dt,
                         std::array<bool, 2>& landed,
                         std::array<bool, 2>& lifted,
                         std::array<bool, 2>& hardLift) {
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        const std::size_t i = index(hand);
        Rest& r = rests_[i];
        // Hard evidence of the thumb elsewhere: a face button pressed, or its own stick out of the centre
        // (the thumb on it, on a runtime that reports no stick touch too).
        RestTouchStep step = r.touch.update(
            {frame.rest[i] && hands_.hasRest[i], frame.thumbElsewhere[i], frame.faceButtons[i] || out(hand)},
            dt);
        // Edge: a gap past the debounce, the other stick centred a while and nothing picked or cancelled, is
        // the thumb tapping the rest again.
        if (step.bridged && settings_.mode == RestWheelMode::Edge &&
            step.lostSeconds >= kRestDebounceSeconds && !step.veto && route_ == Route::Idle && !r.cancelled &&
            centredSeconds_[index(otherHand(hand))] >= kOwnStickVoidSeconds) {
            r.touch.land();
            step.landed = true;
        }
        stats_.rawTouches[i] += step.rawTouch ? 1 : 0;
        stats_.shortTouches[i] += step.shortTouch ? 1 : 0;
        stats_.vetoedLandings[i] += step.vetoedLanding ? 1 : 0;
        stats_.bridgedGaps[i] += step.bridged ? 1 : 0;
        stats_.stuckVetoes[i] += step.stuckVeto ? 1 : 0;
        stats_.cutLifts[i] += step.cutLift ? 1 : 0;
        if (step.landed) {
            r.cancelled = false;
        }
        landed[i] = step.landed;
        lifted[i] = step.lifted;
        hardLift[i] = step.hardLift;
    }
}

bool RestWheel::holds(Hand hand, bool blocked) const {
    // The contact, not the sensor: a frame without the touch while it registers does not drop the stick.
    const RestTouchFilter& r = rests_[index(hand)].touch;
    const Hand other = otherHand(hand);
    return settings_.mode == RestWheelMode::Full && r.contact() && hasRole(other) && route_ == Route::Idle &&
           !blocked && !latched_[index(other)] &&
           (held_[index(other)] ||
            outSeconds_[index(other)] <= r.contactSeconds() + kHeldLeadSeconds + kTimingSlackSeconds);
}

bool RestWheel::pushWaits(Hand rest, Hand stick) const {
    const Rest& r = rests_[index(rest)];
    switch (settings_.mode) {
    case RestWheelMode::Off:
        return false;
    case RestWheelMode::Edge:
        return route_ == Route::Idle && hasRole(stick) && r.landingValid &&
               r.sinceLanding <= settings_.windowSeconds;
    case RestWheelMode::Full:
        return route_ == Route::Taken && stickHand_ == stick && restHand_ == rest;
    case RestWheelMode::Extreme: {
        // The turn hand's rest makes the other stick pick, the other rest gives the turn stick back: either
        // way the lift changes whose the stick is.
        const bool waiting = route_ == Route::Taken && stickHand_ == stick;
        const bool afterLift = extremeStickIf(rest, false) == stick;
        return (route_ == Route::Idle || waiting) && waiting != afterLift;
    }
    }
    return false;
}

bool RestWheel::wheelTakes(Hand rest, Hand stick) const {
    const Rest& r = rests_[index(rest)];
    switch (settings_.mode) {
    case RestWheelMode::Off:
        return false;
    case RestWheelMode::Edge:
        return r.touch.touched() && pushWaits(rest, stick);
    case RestWheelMode::Full:
        return r.touch.touched() && route_ == Route::Taken && stickHand_ == stick;
    case RestWheelMode::Extreme:
        return extremeStick() == stick;
    }
    return false;
}

void RestWheel::holdOverPushes(const std::array<bool, 2>& crossed,
                               bool stopped,
                               std::array<bool, 2>& fresh,
                               std::array<std::optional<Axis2>, 2>& toWheel,
                               RestWheelOutput& o) {
    for (const Hand stick : {Hand::Left, Hand::Right}) {
        const std::size_t s = index(stick);
        const Hand rest = otherHand(stick);
        const RestTouchFilter& r = rests_[index(rest)].touch;
        const bool deciding = r.touched() && !r.raw();
        if (holdOver_[s]) {
            if (magnitude(sticks_[s]) > magnitude(pushPeak_[s])) {
                pushPeak_[s] = sticks_[s];
            }
            if (!stopped && deciding) {
                continue; // still waiting, the stick out or already back
            }
            // The touch came back or lifted (or play stopped): the pick starts now, or the game has the
            // stick at once. A push short of its furthest point by now (on its way back, or back in the
            // centre) is played once from that point, so a flick still snaps, quick-switches or commits.
            holdOver_[s] = false;
            const bool fallen = magnitude(sticks_[s]) < magnitude(pushPeak_[s]) - kOutwardStep;
            if (stopped) {
                givenBack_[s] = out(stick);
            } else if (wheelTakes(rest, stick)) {
                fresh[s] = true;
                ++stats_.heldPushes[index(rest)];
                if (fallen) {
                    toWheel[s] = pushPeak_[s];
                }
            } else {
                givenBack_[s] = out(stick);
                // The mapper plays it to the turn stick's gestures and turn, or to the movement: a tap of the
                // move stick inside a gap is one frame of movement at its furthest point.
                if (fallen) {
                    o.replay[s] = pushPeak_[s];
                }
            }
        } else if (crossed[s] && deciding && !stopped && !latched_[s] && pushWaits(rest, stick)) {
            holdOver_[s] = true;
            pushPeak_[s] = sticks_[s];
        }
    }
}

} // namespace evr::input
