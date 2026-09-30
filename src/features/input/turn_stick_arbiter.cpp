#include "features/input/turn_stick_arbiter.hpp"

#include "common/finite.hpp"

#include <cmath>
#include <numbers>

namespace evr::input {

namespace {

constexpr float kMaxHoldSeconds = 5.0f;

TurnStickSettings sanitized(const TurnStickSettings& settings) {
    const bool usable = finiteInRange(settings.centreRadius, 0.0f, 0.9f) &&
                        finiteInRange(settings.turnClaim, settings.centreRadius, 0.9f) &&
                        finiteInRange(settings.gestureEngage, settings.centreRadius, 1.0f) &&
                        finiteInRange(settings.claimConeDegrees, 0.0f, 89.0f) &&
                        finiteInRange(settings.stayConeDegrees, settings.claimConeDegrees, 89.0f) &&
                        finiteInRange(settings.holdSeconds, 0.0f, kMaxHoldSeconds);
    return usable ? settings : TurnStickSettings{};
}

// True if the stick lies within `coneDegrees` of straight up (sign > 0) or down (sign < 0).
bool inVerticalCone(Axis2 stick, float sign, float coneDegrees) {
    if (stick.y * sign <= 0.0f) {
        return false;
    }
    const float coneRadians = coneDegrees * std::numbers::pi_v<float> / 180.0f;
    return std::fabs(stick.x) <= magnitude(stick) * std::sin(coneRadians);
}

} // namespace

TurnStickArbiter::TurnStickArbiter(TurnStickSettings settings) : settings_(sanitized(settings)) {}

TurnStickOutput TurnStickArbiter::update(Axis2 stick, float dtSeconds, bool wheelHeld) {
    if (isFinite(stick)) {
        lastStick_ = stick;
    } else {
        stick = lastStick_;
        dtSeconds = 0.0f;
    }

    if (wheelHeld) {
        return pointAtWheel(stick);
    }

    if (magnitude(stick) <= settings_.centreRadius) {
        TurnStickOutput output;
        output.downTap = intent_ == SweepIntent::Down && !holdReached_;
        intent_ = SweepIntent::None;
        downSeconds_ = 0.0f;
        holdReached_ = false;
        return output;
    }

    if (intent_ == SweepIntent::None) {
        intent_ = claim(stick);
        // The claim frame is time zero, like a button press.
        downSeconds_ = 0.0f;
    } else if (intent_ == SweepIntent::Down) {
        downSeconds_ += dtSeconds;
    }

    TurnStickOutput output;
    switch (intent_) {
    case SweepIntent::Turn:
        output.turnAllowed = true;
        break;
    case SweepIntent::Up:
        output.up = true;
        break;
    case SweepIntent::Down:
        return continueDown(stick);
    case SweepIntent::None:
    case SweepIntent::Cancelled:
        break;
    }
    return output;
}

SweepIntent TurnStickArbiter::claim(Axis2 stick) const {
    const bool upCone = inVerticalCone(stick, 1.0f, settings_.claimConeDegrees);
    const bool downCone = inVerticalCone(stick, -1.0f, settings_.claimConeDegrees);
    const bool engaged = magnitude(stick) >= settings_.gestureEngage;
    if (upCone && engaged) {
        return SweepIntent::Up;
    }
    if (downCone && engaged) {
        return SweepIntent::Down;
    }
    if (!upCone && !downCone && std::fabs(stick.x) >= settings_.turnClaim) {
        return SweepIntent::Turn;
    }
    return SweepIntent::None;
}

TurnStickOutput TurnStickArbiter::pointAtWheel(Axis2 stick) {
    intent_ = magnitude(stick) <= settings_.centreRadius ? SweepIntent::None : SweepIntent::Cancelled;
    downSeconds_ = 0.0f;
    holdReached_ = false;
    TurnStickOutput output;
    output.wheelPointer = stick;
    return output;
}

TurnStickOutput TurnStickArbiter::continueDown(Axis2 stick) {
    if (!holdReached_) {
        if (!inVerticalCone(stick, -1.0f, settings_.stayConeDegrees)) {
            intent_ = SweepIntent::Cancelled;
            return {};
        }
        holdReached_ = downSeconds_ >= settings_.holdSeconds;
    }
    TurnStickOutput output;
    if (holdReached_) {
        output.downHold = true;
        output.wheelPointer = stick;
    }
    return output;
}

} // namespace evr::input
