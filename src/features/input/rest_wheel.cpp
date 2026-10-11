#include "features/input/rest_wheel.hpp"

#include "common/finite.hpp"

#include <algorithm>
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

RestWheelLoss restWheelLoss(RestWheelMode mode, const RestWheelHands& hands, std::array<bool, 2> bound) {
    RestWheelSettings settings;
    settings.mode = mode;
    RestWheelHands left = hands;
    bool taken = false;
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        taken = taken || (hands.hasRest[index(hand)] && bound[index(hand)]);
        left.hasRest[index(hand)] = hands.hasRest[index(hand)] && !bound[index(hand)];
    }
    if (!taken || !RestWheel(settings, hands).usable()) {
        return RestWheelLoss::None;
    }
    return RestWheel(settings, left).usable() ? RestWheelLoss::OtherRest : RestWheelLoss::Off;
}

RestWheel::RestWheel(RestWheelSettings settings, RestWheelHands hands) : settings_(settings), hands_(hands) {
    settings_.windowSeconds = finiteInRangeOr(settings_.windowSeconds, kMinEdgeWindowSeconds,
                                              kMaxEdgeWindowSeconds, kEdgeWindowSeconds);
    settings_.holdOverSeconds = finiteInRangeOr(settings_.holdOverSeconds, kMinRestHoldOverSeconds,
                                                kMaxRestHoldOverSeconds, kRestHoldOverSeconds);
    for (Rest& r : rests_) {
        r.touch = RestTouchFilter(settings_.holdOverSeconds);
    }
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
    const bool picking = route_ == Route::Pointing || route_ == Route::Open;
    if (settings_.mode == RestWheelMode::Extreme) {
        // The other rest ends a pick under way only once its touch has lasted the debounce: a brush does not
        // close an open wheel on its highlight.
        const Hand restore = restoreHand();
        const bool brush = picking && rests_[index(restore)].touch.onSeconds() < kRestDebounceSeconds;
        return (brush ? extremeStickIf(restore, false) : extremeStick()) != stickHand_;
    }
    // Edge and full need the rest only to start a pick: one under way ends on the stick.
    return !picking && !rests_[index(restHand_)].touch.touched();
}

std::optional<Hand> RestWheel::extremeStick() const {
    const Hand turn = hands_.turnStick.value_or(Hand::Right);
    return extremeStickIf(turn, rests_[index(turn)].touch.touched());
}

std::optional<Hand> RestWheel::extremeStickIf(Hand hand, bool touched) const {
    const Hand turn = hands_.turnStick.value_or(Hand::Right);
    const Hand restore = restoreHand();
    const auto rests = [&](Hand h) {
        return h == hand ? touched : rests_[index(h)].touch.touched();
    };
    if (rests(restore)) {
        return std::nullopt;
    }
    if (rests(turn)) {
        return hasRole(restore) ? std::optional<Hand>(restore) : std::nullopt;
    }
    return turn;
}

bool RestWheel::restCancels() const {
    const RestTouchFilter& restore = rests_[index(restoreHand())].touch;
    return settings_.mode == RestWheelMode::Extreme && restore.touched() &&
           restore.onSeconds() >= kRestDebounceSeconds;
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
        crossedNow_[i] = crossed[i];
        centredSeconds_[i] = out(hand) ? 0.0f : centredSeconds_[i] + dt;
        // From the frame the stick is first out, as the touch's times count from the frame it changed: this
        // frame's dt passed before the stick was out.
        outSeconds_[i] = out(hand) && wasOut ? outSeconds_[i] + dt : 0.0f;
        sinceFaceButton_[i] = frame.faceButtons[i] ? 0.0f : sinceFaceButton_[i] + dt;
        latched_[i] = latched_[i] && out(hand);
    }

    std::array<bool, 2> landed{};
    std::array<bool, 2> lifted{};
    std::array<bool, 2> hardLift{};
    debounce(frame, dt, landed, lifted, hardLift);
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        Rest& r = rests_[index(hand)];
        const Hand stick = otherHand(hand);
        if (landed[index(hand)]) {
            ++stats_.landings[index(hand)];
            if (r.touch.sinceLift() <= kQuickReturnSeconds) {
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
            // While touched: a flick begun with the touch picks once the route takes the stick (which may be
            // a frame later, after a down sweep the hold cancels).
            held_[index(stick)] = holds(hand, frame.blocked);
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
        } else if (r.touch.touched()) {
            r.sinceLanding += dt;
        }
        if (lifted[index(hand)]) {
            r.landingValid = false;
            r.chainPending = false;
        }
        // Picks in a row: the window opens again once the stick has come back to the centre.
        if (r.touch.touched() && r.chainPending && route_ == Route::Idle &&
            centredSeconds_[index(stick)] >= kRearmGapSeconds) {
            r.chainPending = false;
            r.sinceLanding = 0.0f;
            r.landingValid = !out(hand);
        }
    }

    // A push during a hold-over waits for the touch to decide.
    givenBack_ = {};
    handBack_ = {};
    std::array<bool, 2> fresh{};
    std::array<std::optional<Axis2>, 2> toWheel{};
    holdOverPushes(crossed, frame.blocked || frame.sweepDown, fresh, toWheel, o);

    // Edge and full: the thumb pressed a button or pushed its own stick while a pick is still pointing with
    // nothing committed: the pick is cancelled. A push begun just before the thumb left
    // (kTurnBeforeLiftSeconds) was a turn: the stick goes to the game as a fresh push. One older was a pick:
    // the stick stays out of play until it is back in the centre (no snap or quick switch from it).
    if (route_ == Route::Pointing && committed_ == WheelDirection::None && hardLift[index(restHand_)] &&
        (settings_.mode == RestWheelMode::Edge || settings_.mode == RestWheelMode::Full)) {
        const float before = outSeconds_[index(stickHand_)] - rests_[index(restHand_)].touch.gapSeconds();
        if (before <= kTurnBeforeLiftSeconds + kTimingSlackSeconds) {
            givenBack_[index(stickHand_)] = true;
            handBack_[index(stickHand_)] = true;
        }
        endPointing(false, o);
    }

    // A held push short of its furthest point that the wheel takes is played from that point for this frame.
    const std::array<Axis2, 2> actual = sticks_;
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        if (toWheel[index(hand)]) {
            sticks_[index(hand)] = *toWheel[index(hand)];
        }
    }
    if (route_ != Route::Idle) {
        const float minHold = finiteInRangeOr(frame.minWheelHoldSeconds, kMinWheelHoldSeconds,
                                              kMaxWheelHoldSeconds, kMinWheelHoldSeconds);
        continueRoute(frame.blocked || frame.sweepDown, dt, minHold, o);
    }
    if (route_ == Route::Idle && !frame.blocked && !frame.sweepDown) {
        startRoute(crossed, fresh, o);
    }
    if (route_ == Route::Taken && toWheel[index(stickHand_)]) {
        arm(o);
    }
    if (route_ == Route::Pointing) {
        point(dt, o);
    }
    sticks_ = actual;

    // While touched: a stick that left the centre after its rest's contact began (or kHeldLeadSeconds
    // before) is held until the touch registers and a route takes it, or the contact lapses.
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        const bool pending = !rests_[index(hand)].touch.touched() || landed[index(hand)];
        held_[index(otherHand(hand))] = pending && holds(hand, frame.blocked);
    }

    const Axis2 stick = sticks_[index(stickHand_)];
    if (route_ == Route::Open || route_ == Route::Closing) {
        o.wheelDown = true;
        o.pointer = route_ == Route::Open && magnitude(stick) >= kSelectThreshold ? stick : lastUnit_;
    }
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        o.taken[index(hand)] = latched_[index(hand)] || held_[index(hand)] || holdOver_[index(hand)] ||
                               handBack_[index(hand)] || (route_ != Route::Idle && hand == stickHand_);
        o.deferred[index(hand)] = holdOver_[index(hand)] || handBack_[index(hand)];
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
        } else if (this->out(s) && !latched_[index(s)] && !holdOver_[index(s)]) {
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

void RestWheel::startRoute(const std::array<bool, 2>& crossed,
                           const std::array<bool, 2>& fresh,
                           RestWheelOutput& out) {
    switch (settings_.mode) {
    case RestWheelMode::Off:
        return;
    case RestWheelMode::Edge:
        for (const Hand hand : {Hand::Left, Hand::Right}) {
            Rest& r = rests_[index(hand)];
            const Hand stick = otherHand(hand);
            // A push held during the hold-over (fresh) picks once the touch is back.
            if (r.touch.touched() && r.landingValid && r.sinceLanding <= settings_.windowSeconds &&
                hasRole(stick) && (crossed[index(stick)] || fresh[index(stick)]) && !latched_[index(stick)] &&
                !holdOver_[index(stick)]) {
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
            const Rest& r = rests_[index(hand)];
            // A brush while the stick turns or moves leaves it alone: a stick already out (not held with the
            // touch) is taken only once the touch has been on the debounce (gaps not counted).
            const bool brush =
                this->out(stick) && !held_[index(stick)] && r.touch.onSeconds() < kRestDebounceSeconds;
            if (r.touch.touched() && hasRole(stick) && !brush) {
                restHand_ = hand;
                stickHand_ = stick;
                route_ = Route::Taken;
                // A stick held while the touch registered picks; one already out before it waits for the
                // centre.
                if (held_[index(stick)] && this->out(stick)) {
                    ++stats_.heldPushes[index(hand)];
                } else {
                    // A stick out since before waits for the centre; one out from this frame picks.
                    latched_[index(stick)] =
                        latched_[index(stick)] || (this->out(stick) && !crossedNow_[index(stick)]);
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
            // A push held during the other rest's hold-over, or one begun this frame (the thumb from the turn
            // hand's rest to its own stick), picks; any other stick out waits for the centre.
            latched_[index(stickHand_)] =
                latched_[index(stickHand_)] ||
                (this->out(stickHand_) && !fresh[index(stickHand_)] && !crossedNow_[index(stickHand_)]);
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
    // Going further out, no hysteresis. Weapon by direction: a direction with no weapon leaves its eighth to
    // its nearest neighbours with one, and the dwell runs on the direction picked. The game's wheel keeps its
    // eight.
    const WheelDirection current = outward ? WheelDirection::None : octant_;
    const WheelDirection direction =
        settings_.pick == RestWheelPick::Slots
            ? slotDirection(stick, current, settings_.directions, kSelectThreshold, kOctantHysteresisDegrees)
            : octantOf(stick, current);
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
    // A stick out stays out of play until it is back in the centre, except one handed to the game and one
    // that left the centre this very frame (a fresh push).
    latched_[index(stickHand_)] =
        latched_[index(stickHand_)] ||
        (out(stickHand_) && !givenBack_[index(stickHand_)] && !crossedNow_[index(stickHand_)]);
    // Only a pick opens the window again: after a cancel a resting thumb has to land again, so flicks that
    // never settle (snap turns) are never taken over one after another.
    if (picked && settings_.mode == RestWheelMode::Edge && rests_[index(restHand_)].touch.touched()) {
        rests_[index(restHand_)].chainPending = true;
    }
    if (!picked) {
        rests_[index(restHand_)].cancelled = true;
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
        r.cancelled = true;
    }
    // A push held during a hold-over is used up too.
    givenBack_ = {};
    crossedNow_ = {};
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        latched_[index(hand)] = latched_[index(hand)] || holdOver_[index(hand)];
        holdOver_[index(hand)] = false;
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
