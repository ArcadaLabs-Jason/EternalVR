#include "features/input/rest_wheel.hpp"

#include "common/finite.hpp"

#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>

namespace evr::input {

namespace {

constexpr float kDegrees = std::numbers::pi_v<float> / 180.0f;

std::size_t index(Hand hand) {
    return static_cast<std::size_t>(hand);
}

Axis2 unit(Axis2 stick) {
    const float length = magnitude(stick);
    return length > 0.0f ? stick * (1.0f / length) : Axis2{};
}

// The direction the stick points at, kept while the stick stays within kOctantHysteresisDegrees past the edge
// of `current`'s eighth, so a stick resting on a boundary does not flip between two directions.
WheelDirection octantOf(Axis2 stick, WheelDirection current) {
    const WheelDirection nearest = wheelDirection(stick, kSelectThreshold);
    if (nearest == WheelDirection::None || current == WheelDirection::None || nearest == current) {
        return nearest;
    }
    const float centre = static_cast<float>(current) * 45.0f * kDegrees;
    const float off = std::remainder(std::atan2(stick.y, stick.x) - centre, 2.0f * std::numbers::pi_v<float>);
    return std::fabs(off) <= (22.5f + kOctantHysteresisDegrees) * kDegrees ? current : nearest;
}

} // namespace

float wheelHoldSeconds(std::optional<float> openDelayMilliseconds) {
    if (!openDelayMilliseconds || !std::isfinite(*openDelayMilliseconds) || *openDelayMilliseconds <= 0.0f) {
        return kMinWheelHoldSeconds;
    }
    const float seconds = *openDelayMilliseconds / 1000.0f + kWheelOpenMarginSeconds;
    return std::fmin(std::fmax(seconds, kMinWheelHoldSeconds), kMaxWheelHoldSeconds);
}

const char* restWheelModeName(RestWheelMode mode) {
    switch (mode) {
    case RestWheelMode::Off:
        return "off";
    case RestWheelMode::Edge:
        return "edge";
    case RestWheelMode::Full:
        return "full";
    case RestWheelMode::Extreme:
        return "extreme";
    }
    return "off";
}

const char* restWheelPickName(RestWheelPick pick) {
    return pick == RestWheelPick::Slots ? "slots" : "wheel";
}

const char* restWheelEventName(RestWheelEvent event) {
    switch (event) {
    case RestWheelEvent::None:
        return "none";
    case RestWheelEvent::Voided:
        return "voided";
    case RestWheelEvent::Armed:
        return "armed";
    case RestWheelEvent::Opened:
        return "opened";
    case RestWheelEvent::Picked:
        return "picked";
    case RestWheelEvent::Cancelled:
        return "cancelled";
    case RestWheelEvent::Released:
        return "released";
    }
    return "none";
}

RestWheel::RestWheel(RestWheelSettings settings, RestWheelHands hands) : settings_(settings), hands_(hands) {
    settings_.windowSeconds = finiteInRangeOr(settings_.windowSeconds, kMinEdgeWindowSeconds,
                                              kMaxEdgeWindowSeconds, kEdgeWindowSeconds);
    switch (settings_.mode) {
    case RestWheelMode::Off:
        usable_ = false;
        break;
    case RestWheelMode::Edge:
    case RestWheelMode::Full:
        for (const Hand hand : {Hand::Left, Hand::Right}) {
            usable_ = usable_ || (hands_.hasRest[index(hand)] && hasRole(otherHand(hand)));
        }
        break;
    case RestWheelMode::Extreme:
        usable_ = hands_.turnStick.has_value() && hands_.hasRest[index(restoreHand())];
        break;
    }
}

bool RestWheel::hasRole(Hand hand) const {
    return hands_.moveStick == hand || hands_.turnStick == hand;
}

Hand RestWheel::restoreHand() const {
    const Hand turn = hands_.turnStick.value_or(Hand::Right);
    return hands_.moveStick && *hands_.moveStick != turn ? *hands_.moveStick : otherHand(turn);
}

bool RestWheel::out(Hand hand) const {
    return magnitude(sticks_[index(hand)]) > kArmDeadzone;
}

void RestWheel::note(RestWheelOutput& out, RestWheelEvent event) {
    if (out.event == RestWheelEvent::None || out.event == RestWheelEvent::Voided) {
        out.event = event;
    }
}

bool RestWheel::restGone() const {
    if (settings_.mode == RestWheelMode::Extreme) {
        return extremeStick() != stickHand_;
    }
    return !rests_[index(restHand_)].touched;
}

std::optional<Hand> RestWheel::extremeStick() const {
    const Hand turn = hands_.turnStick.value_or(Hand::Right);
    const Hand restore = restoreHand();
    if (rests_[index(restore)].touched) {
        return std::nullopt;
    }
    if (rests_[index(turn)].touched) {
        return hasRole(restore) ? std::optional<Hand>(restore) : std::nullopt;
    }
    return turn;
}

bool RestWheel::restCancels() const {
    return settings_.mode == RestWheelMode::Extreme && rests_[index(restoreHand())].touched;
}

void RestWheel::debounce(const RestWheelFrame& frame,
                         float dt,
                         std::array<bool, 2>& landed,
                         std::array<bool, 2>& lifted) {
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        Rest& r = rests_[index(hand)];
        const bool raw = frame.rest[index(hand)] && hands_.hasRest[index(hand)];
        if (raw != r.raw) {
            if (raw) {
                ++stats_.rawTouches[index(hand)];
            }
            // The state that ends now never lasted the debounce: a touch that never registered, or a gap in a
            // registered touch, bridged.
            if (r.rawSeconds < kRestDebounceSeconds && raw == r.touched) {
                ++(raw ? stats_.bridgedGaps : stats_.shortTouches)[index(hand)];
            }
            r.raw = raw;
            r.rawSeconds = 0.0f;
        } else {
            r.rawSeconds += dt;
        }
        if (r.touched != r.raw && r.rawSeconds >= kRestDebounceSeconds) {
            r.touched = r.raw;
            (r.touched ? landed : lifted)[index(hand)] = true;
        }
    }
}

RestWheelOutput RestWheel::update(const RestWheelFrame& frame, float dtSeconds) {
    RestWheelOutput o;
    if (!usable_) {
        return o;
    }
    const float dt = std::isfinite(dtSeconds) && dtSeconds > 0.0f ? dtSeconds : 0.0f;
    std::array<bool, 2> crossed{};
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        const std::size_t i = index(hand);
        const bool wasOut = out(hand);
        if (isFinite(frame.sticks[i])) {
            sticks_[i] = frame.sticks[i];
        }
        crossed[i] = out(hand) && !wasOut;
        centredSeconds_[i] = out(hand) ? 0.0f : centredSeconds_[i] + dt;
        outSeconds_[i] = out(hand) ? outSeconds_[i] + dt : 0.0f;
        sinceFaceButton_[i] = frame.faceButtons[i] ? 0.0f : sinceFaceButton_[i] + dt;
        latched_[i] = latched_[i] && out(hand);
    }

    std::array<bool, 2> landed{};
    std::array<bool, 2> lifted{};
    debounce(frame, dt, landed, lifted);
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        Rest& r = rests_[index(hand)];
        const Hand stick = otherHand(hand);
        if (landed[index(hand)]) {
            ++stats_.landings[index(hand)];
            if (r.sinceLift <= kQuickReturnSeconds) {
                ++stats_.quickReturns[index(hand)];
            }
            if (out(stick)) {
                ++stats_.stickOut[index(hand)];
                if (outSeconds_[index(stick)] <= kRecentStickSeconds) {
                    ++stats_.stickOutRecent[index(hand)];
                }
            }
            r.sinceLanding = 0.0f;
            r.chainPending = false;
            RestWheelVoid why = RestWheelVoid::None;
            if (out(hand) || out(stick)) {
                why = RestWheelVoid::StickOut;
            } else if (centredSeconds_[index(hand)] < kOwnStickVoidSeconds) {
                why = RestWheelVoid::OwnStick;
            } else if (sinceFaceButton_[index(hand)] < kOwnButtonVoidSeconds) {
                why = RestWheelVoid::OwnButton;
            }
            r.landingValid = why == RestWheelVoid::None;
            if (!r.landingValid && settings_.mode == RestWheelMode::Edge && route_ == Route::Idle &&
                hasRole(stick)) {
                note(o, RestWheelEvent::Voided);
                o.voided = why;
                o.restHand = hand;
                o.stickHand = stick;
            }
        } else if (r.touched) {
            r.sinceLanding += dt;
        }
        if (lifted[index(hand)]) {
            r.landingValid = false;
            r.chainPending = false;
            r.sinceLift = 0.0f;
        } else if (!r.touched) {
            r.sinceLift = std::fmin(r.sinceLift + dt, kLongAgoSeconds);
        }
        // Picks in a row: the window opens again once the stick has come back to the centre.
        if (r.touched && r.chainPending && route_ == Route::Idle &&
            centredSeconds_[index(stick)] >= kRearmGapSeconds) {
            r.chainPending = false;
            r.sinceLanding = 0.0f;
            r.landingValid = !out(hand);
        }
    }

    if (route_ != Route::Idle) {
        const float minHold = finiteInRangeOr(frame.minWheelHoldSeconds, kMinWheelHoldSeconds,
                                              kMaxWheelHoldSeconds, kMinWheelHoldSeconds);
        continueRoute(frame.blocked || frame.sweepDown, dt, minHold, o);
    }
    if (route_ == Route::Idle && !frame.blocked && !frame.sweepDown) {
        startRoute(crossed, o);
    }
    if (route_ == Route::Pointing) {
        point(dt, o);
    }

    // While touched: a stick that left the centre after its rest's touch was sensed (or kHeldLeadSeconds
    // before) is held until the touch registers or goes.
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        const Rest& r = rests_[index(hand)];
        const Hand other = otherHand(hand);
        const bool pending = settings_.mode == RestWheelMode::Full && r.raw && !r.touched && hasRole(other) &&
                             route_ == Route::Idle && !frame.blocked && !latched_[index(other)];
        held_[index(other)] =
            pending && (held_[index(other)] || outSeconds_[index(other)] <= r.rawSeconds + kHeldLeadSeconds);
    }

    const Axis2 stick = sticks_[index(stickHand_)];
    if (route_ == Route::Open || route_ == Route::Closing) {
        o.wheelDown = true;
        o.pointer = route_ == Route::Open && magnitude(stick) >= kSelectThreshold ? stick : lastUnit_;
    }
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        o.taken[index(hand)] =
            latched_[index(hand)] || held_[index(hand)] || (route_ != Route::Idle && hand == stickHand_);
    }
    return o;
}

void RestWheel::continueRoute(bool blocked, float dt, float minHold, RestWheelOutput& out) {
    const Hand s = stickHand_;
    const bool centred = !this->out(s);
    switch (route_) {
    case Route::Idle:
        break;
    case Route::Taken:
        if (restGone() || blocked) {
            // Blocked (piloting a demon, a forced view, a cutscene): the stick goes back to the game. A stick
            // out stays out of play until it is back in the centre, so a push made now starts nothing later.
            finishRoute(false);
        } else if (this->out(s) && !latched_[index(s)]) {
            arm(out);
        }
        break;
    case Route::Pointing:
        if (blocked) {
            endPointing(false, out);
        } else if (centred) {
            endPointing(true, out);
        } else if (restGone()) {
            endPointing(!restCancels(), out);
        }
        break;
    case Route::Open:
    case Route::Closing:
        pressSeconds_ += dt;
        if (route_ == Route::Open) {
            if (magnitude(sticks_[index(s)]) >= kSelectThreshold) {
                lastUnit_ = unit(sticks_[index(s)]);
            }
            if (blocked || centred || restGone()) {
                route_ = Route::Closing;
            }
        }
        if (route_ == Route::Closing && pressSeconds_ >= minHold) {
            out.tick[index(s)] = WheelTick::Pick;
            note(out, RestWheelEvent::Released);
            out.restHand = restHand_;
            out.stickHand = s;
            out.direction = wheelDirection(lastUnit_, kSelectThreshold);
            finishRoute(true);
        }
        break;
    }
}

void RestWheel::startRoute(const std::array<bool, 2>& crossed, RestWheelOutput& out) {
    switch (settings_.mode) {
    case RestWheelMode::Off:
        return;
    case RestWheelMode::Edge:
        for (const Hand hand : {Hand::Left, Hand::Right}) {
            Rest& r = rests_[index(hand)];
            const Hand stick = otherHand(hand);
            if (r.touched && r.landingValid && r.sinceLanding <= settings_.windowSeconds && hasRole(stick) &&
                crossed[index(stick)] && !latched_[index(stick)]) {
                r.landingValid = false;
                restHand_ = hand;
                stickHand_ = stick;
                arm(out);
                out.sinceTouch = r.sinceLanding;
                return;
            }
        }
        return;
    case RestWheelMode::Full:
        for (const Hand hand : {Hand::Left, Hand::Right}) {
            const Hand stick = otherHand(hand);
            if (rests_[index(hand)].touched && hasRole(stick)) {
                restHand_ = hand;
                stickHand_ = stick;
                route_ = Route::Taken;
                // A stick held while the touch registered picks; one already out before it waits for the
                // centre.
                if (held_[index(stick)] && this->out(stick)) {
                    ++stats_.heldPushes[index(hand)];
                } else {
                    latched_[index(stick)] = latched_[index(stick)] || this->out(stick);
                }
                held_[index(stick)] = false;
                return;
            }
        }
        return;
    case RestWheelMode::Extreme:
        if (const std::optional<Hand> stick = extremeStick()) {
            const Hand turn = hands_.turnStick.value_or(Hand::Right);
            stickHand_ = *stick;
            restHand_ = *stick == turn ? restoreHand() : turn;
            route_ = Route::Taken;
            latched_[index(stickHand_)] = latched_[index(stickHand_)] || this->out(stickHand_);
        }
        return;
    }
}

void RestWheel::arm(RestWheelOutput& out) {
    route_ = Route::Pointing;
    octant_ = WheelDirection::None;
    dwellSeconds_ = 0.0f;
    committed_ = WheelDirection::None;
    flickCommitted_ = false;
    peak_ = 0.0f;
    out.tick[index(stickHand_)] = WheelTick::Arm;
    note(out, RestWheelEvent::Armed);
    out.restHand = restHand_;
    out.stickHand = stickHand_;
}

void RestWheel::point(float dt, RestWheelOutput& out) {
    const Axis2 stick = sticks_[index(stickHand_)];
    const float length = magnitude(stick);
    const bool outward = length > peak_ + kOutwardStep;
    if (outward) {
        peak_ = length;
    }
    const WheelDirection direction =
        outward ? wheelDirection(stick, kSelectThreshold) : octantOf(stick, octant_);
    if (direction != octant_) {
        octant_ = direction;
        dwellSeconds_ = 0.0f;
    } else if (direction != WheelDirection::None) {
        dwellSeconds_ += dt;
    }
    if (settings_.pick == RestWheelPick::Slots && (committed_ == WheelDirection::None || flickCommitted_) &&
        outward && octant_ != WheelDirection::None && length >= kFlickThreshold) {
        committed_ = octant_;
        flickCommitted_ = true;
        return;
    }
    if (octant_ == WheelDirection::None || dwellSeconds_ < kDwellSeconds) {
        return;
    }
    if (settings_.pick == RestWheelPick::Slots) {
        committed_ = octant_;
        flickCommitted_ = false;
        return;
    }
    route_ = Route::Open;
    pressSeconds_ = 0.0f;
    lastUnit_ = unit(stick);
    note(out, RestWheelEvent::Opened);
    out.restHand = restHand_;
    out.stickHand = stickHand_;
    out.direction = octant_;
}

void RestWheel::endPointing(bool pick, RestWheelOutput& out) {
    const auto slot = pick && settings_.pick == RestWheelPick::Slots
                          ? weaponSlotFor(settings_.directions, committed_)
                          : std::nullopt;
    if (slot) {
        out.slot = slot;
        out.tick[index(stickHand_)] = WheelTick::Pick;
        note(out, RestWheelEvent::Picked);
        out.direction = committed_;
    } else {
        note(out, RestWheelEvent::Cancelled);
        out.direction = octant_;
    }
    out.restHand = restHand_;
    out.stickHand = stickHand_;
    out.peak = peak_;
    finishRoute(slot.has_value());
}

void RestWheel::finishRoute(bool picked) {
    latched_[index(stickHand_)] = latched_[index(stickHand_)] || out(stickHand_);
    // Only a pick opens the window again: after a cancel a resting thumb has to land again, so flicks that
    // never settle (snap turns) are never taken over one after another.
    if (picked && settings_.mode == RestWheelMode::Edge && rests_[index(restHand_)].touched) {
        rests_[index(restHand_)].chainPending = true;
    }
    route_ = Route::Idle;
    octant_ = WheelDirection::None;
    dwellSeconds_ = 0.0f;
    committed_ = WheelDirection::None;
    flickCommitted_ = false;
    peak_ = 0.0f;
    pressSeconds_ = 0.0f;
}

void RestWheel::cancel() {
    for (Rest& r : rests_) {
        r.landingValid = false;
        r.chainPending = false;
    }
    switch (route_) {
    case Route::Idle:
    case Route::Closing:
        break;
    case Route::Taken:
    case Route::Pointing:
        finishRoute(false); // a thumb still resting under edge has to land again
        break;
    case Route::Open:
        route_ = Route::Closing;
        break;
    }
}

} // namespace evr::input
