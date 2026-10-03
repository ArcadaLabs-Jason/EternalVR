#include "features/input/menu_release_latch.hpp"

#include <cmath>
#include <cstddef>

namespace evr::input {

namespace {

// Not let go yet. A value that is not finite (a lost action state) is not taken as a release, so a glitch
// does not end the latch.
bool analogHeld(float value, float threshold) {
    return !std::isfinite(value) || value > threshold;
}

bool buttonHeld(const HandState& hand, ButtonInput input, const HeldThresholds& thresholds) {
    switch (input) {
    case ButtonInput::Trigger:
        return analogHeld(hand.trigger, thresholds.trigger);
    case ButtonInput::Grip:
        return analogHeld(hand.grip, thresholds.grip);
    case ButtonInput::StickClick:
        return hand.stickClick;
    case ButtonInput::Primary:
        return hand.primaryButton;
    case ButtonInput::Secondary:
        return hand.secondaryButton;
    case ButtonInput::Face3:
        return hand.face3Button;
    case ButtonInput::Face4:
        return hand.face4Button;
    case ButtonInput::Shoulder:
        return hand.shoulderButton;
    case ButtonInput::Menu:
        return hand.menuButton;
    case ButtonInput::Count:
        break;
    }
    return false;
}

void release(HandState& hand, ButtonInput input) {
    switch (input) {
    case ButtonInput::Trigger:
        hand.trigger = 0.0f;
        break;
    case ButtonInput::Grip:
        hand.grip = 0.0f;
        break;
    case ButtonInput::StickClick:
        hand.stickClick = false;
        break;
    case ButtonInput::Primary:
        hand.primaryButton = false;
        break;
    case ButtonInput::Secondary:
        hand.secondaryButton = false;
        break;
    case ButtonInput::Face3:
        hand.face3Button = false;
        break;
    case ButtonInput::Face4:
        hand.face4Button = false;
        break;
    case ButtonInput::Shoulder:
        hand.shoulderButton = false;
        break;
    case ButtonInput::Menu:
        hand.menuButton = false;
        break;
    case ButtonInput::Count:
        break;
    }
}

bool stickOut(Axis2 stick, float centre) {
    return !isFinite(stick) || magnitude(stick) > centre;
}

} // namespace

MenuReleaseOutput MenuReleaseLatch::update(const InputFrame& in, bool holding) {
    MenuReleaseOutput out;
    out.frame = in;
    out.holdEnded = holding_ && !holding;
    holding_ = holding;
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        HandLatch& latch = latched_[static_cast<std::size_t>(hand)];
        const HandState& raw = in.hand(hand);
        HandState& masked = hand == Hand::Left ? out.frame.left : out.frame.right;
        // Under the hold every input down is latched and the frame passes as it is; after it, an input stays
        // latched, and reads as released, until it is let go.
        for (std::size_t i = 0; i < kButtonInputCount; ++i) {
            const auto input = static_cast<ButtonInput>(i);
            const bool held = buttonHeld(raw, input, thresholds_);
            latch.buttons[i] = held && (holding || latch.buttons[i]);
            if (!holding && latch.buttons[i]) {
                release(masked, input);
            }
        }
        const bool deflected = stickOut(raw.stick, thresholds_.stickCentre);
        latch.stick = deflected && (holding || latch.stick);
        if (!holding && latch.stick) {
            masked.stick = {};
        }
    }
    return out;
}

bool MenuReleaseLatch::anyLatched() const {
    for (const HandLatch& latch : latched_) {
        if (latch.stick) {
            return true;
        }
        for (const bool button : latch.buttons) {
            if (button) {
                return true;
            }
        }
    }
    return false;
}

} // namespace evr::input
