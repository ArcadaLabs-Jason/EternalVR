#include "features/input/reach_use.hpp"

#include "common/finite.hpp"
#include "features/input/gesture_frame.hpp"

#include <algorithm>
#include <cstddef>

namespace evr::input {

namespace {

// Forward slower than this (metres per second) on the way out is a pause: the reach is one movement.
constexpr float kPauseSpeed = 0.15f;

ReachSettings sanitized(ReachSettings s) {
    const ReachSettings d;
    s.reachDistance = finiteInRangeOr(s.reachDistance, 0.2f, 0.9f, d.reachDistance);
    s.rearmDistance = finiteInRangeOr(s.rearmDistance, 0.1f, 0.8f, d.rearmDistance);
    s.rearmDistance = std::min(s.rearmDistance, s.reachDistance);
    s.minSpeed = finiteInRangeOr(s.minSpeed, 0.1f, 3.0f, d.minSpeed);
    s.maxSpeed = finiteInRangeOr(s.maxSpeed, 0.2f, 6.0f, d.maxSpeed);
    s.maxSpeed = std::max(s.maxSpeed, s.minSpeed);
    s.withdrawDistance = finiteInRangeOr(s.withdrawDistance, 0.03f, 0.4f, d.withdrawDistance);
    s.dropAllowance = finiteInRangeOr(s.dropAllowance, 0.0f, 0.3f, d.dropAllowance);
    s.maxOutSeconds = finiteInRangeOr(s.maxOutSeconds, 0.1f, 3.0f, d.maxOutSeconds);
    s.maxTrigger = finiteInRangeOr(s.maxTrigger, 0.0f, 1.0f, d.maxTrigger);
    s.cooldownSeconds = finiteInRangeOr(s.cooldownSeconds, 0.0f, 5.0f, d.cooldownSeconds);
    s.holdSeconds = finiteInRangeOr(s.holdSeconds, 0.0f, 1.0f, d.holdSeconds);
    return s;
}

} // namespace

ReachUse::ReachUse(ReachSettings settings) : settings_(sanitized(settings)) {}

ReachOutput
ReachUse::update(const InputFrame& frame, float dtSeconds, std::array<bool, 2> primed, bool blocked) {
    ReachOutput out;
    if (!settings_.enabled) {
        return out;
    }
    cooldown_ = std::max(0.0f, cooldown_ - dtSeconds);
    holding_ = std::max(0.0f, holding_ - dtSeconds);
    const std::optional<Vec3> forward = headHeading(frame.head);
    if (blocked || !forward) {
        hands_ = {};
        holding_ = 0.0f;
        return out;
    }
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        const std::size_t i = static_cast<std::size_t>(hand);
        HandReach& r = hands_[i];
        const HandState& h = frame.hand(hand);
        if (!handTracked(h)) {
            r = {};
            continue;
        }
        const HeadFramePoint p = inHeadFrame(h.aimPose.position, frame.head.pose.position, *forward);
        const float speed = dot(h.linearVelocity, *forward);
        if (h.trigger >= settings_.maxTrigger || primed[i]) {
            r = {}; // firing, or winding up a swing: no reach until the hand has come back
            continue;
        }
        if (r.out) {
            r.outSeconds += dtSeconds;
            if (p.forward > r.furthest) {
                r.furthest = p.forward;
                r.heightThere = p.up;
            }
            const float back = r.furthest - p.forward;
            const float drop = r.heightThere - p.up;
            if (r.outSeconds > settings_.maxOutSeconds || drop > back + settings_.dropAllowance) {
                r.out = false; // held out (aiming, steadying the weapon) or lowered: no reach
                r.armed = false;
            } else if (back >= settings_.withdrawDistance) {
                if (cooldown_ <= 0.0f && !out.reached) {
                    out.reached = true;
                    out.hand[i] = true;
                    cooldown_ = settings_.cooldownSeconds;
                    holding_ = std::max(settings_.holdSeconds, 1e-3f);
                }
                r.out = false;
                r.armed = false;
            }
        }
        if (p.forward < settings_.rearmDistance) {
            r = {};
            r.armed = true;
            continue;
        }
        if (!r.armed || r.out) {
            continue;
        }
        // One movement out: a hand that pauses on the way (an aim settling) starts its speed over.
        r.peakSpeed = speed < kPauseSpeed ? 0.0f : std::max(r.peakSpeed, speed);
        if (r.peakSpeed > settings_.maxSpeed) {
            r.armed = false; // a punch
        } else if (p.forward >= settings_.reachDistance) {
            if (r.peakSpeed >= settings_.minSpeed) {
                r.out = true;
                r.outSeconds = 0.0f;
                r.furthest = p.forward;
                r.heightThere = p.up;
            } else {
                r.armed = false; // drifted out slowly: no reach
            }
        }
    }
    out.useDown = holding_ > 0.0f;
    return out;
}

} // namespace evr::input
