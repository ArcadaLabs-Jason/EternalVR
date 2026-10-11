// The thumb-rest wheel's touch handling (rest_wheel.hpp): the landing and the lift of each rest, its contact
// and hold-over, and the other stick held while a touch decides.

#include "features/input/rest_wheel.hpp"

#include <algorithm>
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
        const bool raw = frame.rest[i] && hands_.hasRest[i];
        // The thumb elsewhere: hard evidence (a face button pressed, or its own stick out of the centre, the
        // thumb on it, on a runtime that reports no stick touch too), or a touch sensor beside the rest.
        const bool hard = frame.faceButtons[i] || out(hand);
        const bool vetoTouch = frame.thumbElsewhere[i];
        r.vetoSeconds = vetoTouch && r.vetoTouch ? r.vetoSeconds + dt : 0.0f;
        r.vetoToggled = vetoTouch && r.vetoToggled;
        r.vetoTouch = vetoTouch;
        // A touch sensor on all along while the rest comes and goes sees the resting thumb itself.
        const bool stuck = vetoTouch && r.vetoToggled && r.vetoSeconds > kStuckVetoSeconds;
        const bool veto = hard || (vetoTouch && !stuck);
        // A touch sensor cuts a hold-over only once steady (one flickering with the rest cuts nothing), and
        // not when stuck (a press then still cuts).
        const bool elsewhere = hard || (vetoTouch && !stuck && r.vetoSeconds >= kVetoTouchSteadySeconds);
        const bool wentElsewhere = elsewhere && !r.elsewhere;
        r.elsewhere = elsewhere;
        // The thumb moved to a button or the stick: a lost touch lifts at once.
        bool cut = false;
        // Edge: a gap past the debounce, the other stick centred a while and nothing picked or cancelled, is
        // the thumb tapping the rest again.
        bool retap = false;
        if (raw != r.raw) {
            if (raw) {
                ++stats_.rawTouches[i];
                r.vetoToggled = vetoTouch;
                if (r.touched) {
                    ++stats_.bridgedGaps[i]; // back within the hold-over: forgiven
                    retap = settings_.mode == RestWheelMode::Edge && r.rawSeconds >= kRestDebounceSeconds &&
                            !veto && route_ == Route::Idle && !r.cancelled &&
                            centredSeconds_[index(otherHand(hand))] >= kOwnStickVoidSeconds;
                } else if (!r.contact && veto) {
                    ++stats_.vetoedLandings[i];
                }
            } else {
                if (r.rawSeconds < kRestDebounceSeconds) {
                    ++stats_.shortTouches[i];
                }
                cut = r.touched && elsewhere && r.wentElsewhere;
            }
            r.raw = raw;
            r.rawSeconds = 0.0f;
            if (!raw && r.touched && !r.gap) {
                r.gap = true;
                r.gapSeconds = 0.0f;
            } else if (r.gap) {
                r.gapSeconds += dt;
            }
        } else {
            r.rawSeconds += dt;
            if (r.gap) {
                r.gapSeconds += dt;
                r.gap = !(r.raw && r.rawSeconds >= kRestDebounceSeconds);
            }
        }
        if (r.raw) {
            r.contactSeconds = r.contact ? r.contactSeconds + dt : 0.0f;
            r.contact = true;
        } else if (r.contact) {
            r.contactSeconds += dt;
            r.contact = r.rawSeconds < settings_.holdOverSeconds;
        }
        if (r.touched && wentElsewhere) {
            r.wentElsewhere = true;
            cut = cut || !r.raw;
        }
        const float holdOver = std::clamp(r.onSeconds, kRestHoldOverFloorSeconds, settings_.holdOverSeconds);
        if ((!r.touched && r.raw && (!veto || r.contactSeconds >= kRestDebounceSeconds)) || retap) {
            if (!r.touched && !hard && stuck && r.contactSeconds < kRestDebounceSeconds) {
                ++stats_.stuckVetoes[i];
            }
            r.touched = true;
            r.wentElsewhere = false;
            r.onSeconds = 0.0f;
            r.cancelled = false;
            r.gap = false;
            landed[i] = true;
        } else if (r.touched && !r.raw && (cut || r.rawSeconds >= holdOver)) {
            r.touched = false;
            r.wentElsewhere = false;
            r.contact = false;
            r.gap = false; // gapSeconds stays: the cancel in update() reads it
            lifted[i] = true;
            hardLift[i] = cut && hard;
            // Quick returns count from when the touch went, not from the end of the hold-over.
            r.sinceLift = cut ? 0.0f : r.rawSeconds;
            if (cut) {
                ++stats_.cutLifts[i];
            }
        } else if (r.touched && r.raw) {
            r.onSeconds += dt;
        }
    }
}

bool RestWheel::holds(Hand hand, bool blocked) const {
    // The contact, not the sensor: a frame without the touch while it registers does not drop the stick.
    const Rest& r = rests_[index(hand)];
    const Hand other = otherHand(hand);
    return settings_.mode == RestWheelMode::Full && r.contact && hasRole(other) && route_ == Route::Idle &&
           !blocked && !latched_[index(other)] &&
           (held_[index(other)] ||
            outSeconds_[index(other)] <= r.contactSeconds + kHeldLeadSeconds + kTimingSlackSeconds);
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
        return r.touched && pushWaits(rest, stick);
    case RestWheelMode::Full:
        return r.touched && route_ == Route::Taken && stickHand_ == stick;
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
        const Rest& r = rests_[index(rest)];
        const bool deciding = r.touched && !r.raw;
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
