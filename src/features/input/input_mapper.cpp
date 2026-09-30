#include "features/input/input_mapper.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <utility>

namespace evr::input {

namespace {

constexpr std::size_t handIndex(Hand hand) {
    return static_cast<std::size_t>(hand);
}

constexpr std::size_t buttonIndex(ButtonInput input) {
    return static_cast<std::size_t>(input);
}

float sanitizedDt(float dtSeconds) {
    return std::isfinite(dtSeconds) && dtSeconds > 0.0f ? std::min(dtSeconds, kMaxFrameSeconds) : 0.0f;
}

// Policies that take their settings as a whole sanitise them in their own constructors; these are
// the parts the mapper owns or combines.
MapperSettings sanitized(MapperSettings settings) {
    settings.trigger = sanitizedThresholds(settings.trigger, kTriggerThresholds);
    settings.grip = sanitizedThresholds(settings.grip, kGripThresholds);
    settings.move = sanitizedResponse(settings.move, kMoveStickResponse);
    settings.buttonHoldSeconds = TapHoldDetector(settings.buttonHoldSeconds).holdSeconds();
    settings.turnStick = TurnStickArbiter(settings.turnStick).settings();
    settings.turn = TurnPolicy(settings.turn).settings();
    // A sweep only starts turning once the arbiter has claimed it as a turn, so smooth turning ramps
    // up from zero at the claim deflection. Measured from the response deadzone instead, the rate
    // would jump from nothing to a sizeable fraction of full speed the moment the claim is made.
    settings.turn.smoothResponse = startingFrom(settings.turn.smoothResponse, settings.turnStick.turnClaim);
    settings.punch = PunchDetector(settings.punch).settings();
    const ArmGestures arms(settings.throwGesture, settings.swing);
    settings.throwGesture = arms.throwSettings();
    settings.swing = arms.swingSettings();
    return settings;
}

bool bindingActive(PressKind kind, bool down, bool tap, bool hold) {
    switch (kind) {
    case PressKind::WhileDown:
        return down;
    case PressKind::Tap:
        return tap;
    case PressKind::Hold:
        return hold;
    }
    return false;
}

bool gestureActive(StickGesture gesture, const TurnStickOutput& output) {
    switch (gesture) {
    case StickGesture::Up:
        return output.up;
    case StickGesture::DownTap:
        return output.downTap;
    case StickGesture::DownHold:
        return output.downHold;
    }
    return false;
}

} // namespace

InputMapper::InputMapper(BindingProfile profile, MapperSettings settings)
    : profile_(std::move(profile)), settings_(sanitized(settings)), turnStick_(settings_.turnStick),
      turn_(settings_.turn), handsJump_(settings_.handsJump),
      armGestures_(settings_.throwGesture, settings_.swing), punch_(settings_.punch),
      captureChord_(settings_.trigger, settings_.captureButtons), stickChord_(settings_.buttonHoldSeconds) {
    for (HandButtons& hand : hands_) {
        hand.trigger = AnalogButton(settings_.trigger);
        hand.grip = AnalogButton(settings_.grip);
        hand.tapHold.fill(TapHoldDetector(settings_.buttonHoldSeconds));
        hand.tapHold[buttonIndex(ButtonInput::Menu)] =
            TapHoldDetector(settings_.buttonHoldSeconds, settings_.menuTapSeconds);
    }
}

GameInput InputMapper::update(const InputFrame& raw, const MapperContext& context, float dtSeconds) {
    const float dt = sanitizedDt(dtSeconds);
    GameInput input;

    // Both sticks pressed: the recenter chord. The sticks' own bindings see a click only when it is not
    // part of the chord (a single click stays instant; stick_chord.hpp).
    InputFrame frame = raw;
    const StickChordOutput sticks = stickChord_.update(frame.left.stickClick, frame.right.stickClick, dt);
    frame.left.stickClick = sticks.click[handIndex(Hand::Left)];
    frame.right.stickClick = sticks.click[handIndex(Hand::Right)];
    if (settings_.stickChordRecenter && sticks.recenter) {
        game::add(input.down, game::GameAction::Recenter);
    }

    // Left Menu (or under SteamVR Y) held + a trigger pulled: the capture. That press neither taps nor holds,
    // and the trigger is held back meanwhile (capture_chord.hpp).
    const CaptureChordOutput chord = captureChord_.update(frame);
    input.capture = chord.capture;
    HandButtons& left = hands_[handIndex(Hand::Left)];
    if (chord.cancelMenu) {
        left.tapHold[buttonIndex(ButtonInput::Menu)].cancel();
    }
    if (chord.cancelSecondary) {
        left.tapHold[buttonIndex(ButtonInput::Secondary)].cancel();
    }

    for (const Hand hand : {Hand::Left, Hand::Right}) {
        HandButtons& buttons = hands_[handIndex(hand)];
        const ButtonLevels levels = sampleButtons(buttons, frame.hand(hand), dt);
        // A grip steadying the weapon is holding it, not asking for an action. The suppression
        // outlasts the support until the grip is released, and covers the release frame itself so
        // that a tap binding does not fire on it.
        const bool supporting = context.supportHandOnWeapon && hand != profile_.weaponHand;
        buttons.gripHeldForSupport = buttons.gripHeldForSupport || supporting;
        addButtonActions(hand, levels, buttons.gripHeldForSupport, chord.triggerHeldBack[handIndex(hand)],
                         input.down);
        if (!supporting && !levels.down[buttonIndex(ButtonInput::Grip)]) {
            buttons.gripHeldForSupport = false;
        }
    }

    // A button holding the weapon wheel: the turn stick points at it instead of turning or firing its
    // gestures, as under the stick's own down hold.
    const bool wheelFromButton = game::contains(input.down, game::GameAction::WeaponWheel);
    const Axis2 turnStick = profile_.turnStick ? frame.hand(*profile_.turnStick).stick : Axis2{};
    const TurnStickOutput gestures = turnStick_.update(turnStick, dt, wheelFromButton);
    addStickGestureActions(gestures, input.down);
    input.turnDegrees = turn_.update(turnStick, dt, gestures.turnAllowed);
    input.wheelPointer = gestures.wheelPointer;

    const HandState& offHand = frame.hand(otherHand(profile_.weaponHand));
    const float locomotionYaw = locomotion_.update(settings_.locomotionFrame, frame.head, offHand);
    const Axis2 moveStick = profile_.moveStick ? frame.hand(*profile_.moveStick).stick : Axis2{};
    const Axis2 move = applyStickResponse(moveStick, settings_.move);
    input.move = rotateIntoViewFrame(move, locomotionYaw, context.viewYawRadians);

    if (handsJump_.update(frame, context.posture, dt)) {
        game::add(input.down, game::GameAction::Jump);
    }
    // A throw or an overhead swing holds back its hand's punch: the gesture's own motion would punch too.
    const ArmGestureOutput arms = armGestures_.update(frame, profile_.weaponHand, dt);
    if (arms.thrown) {
        game::add(input.down, game::GameAction::Equipment);
    }
    if (arms.swung) {
        game::add(input.down, game::GameAction::Crucible);
    }
    input.thrown = arms.thrown;
    input.swung = arms.swung;
    if (punch_.update(frame, arms.heldBack)) {
        game::add(input.down, game::GameAction::Melee);
        input.punch = punch_.punched();
    }

    input.pressed = input.down & ~previousDown_;
    input.released = previousDown_ & ~input.down;
    previousDown_ = input.down;
    return input;
}

InputMapper::ButtonLevels
InputMapper::sampleButtons(HandButtons& buttons, const HandState& hand, float dtSeconds) {
    ButtonLevels levels;
    levels.down[buttonIndex(ButtonInput::Trigger)] = buttons.trigger.update(hand.trigger).down;
    levels.down[buttonIndex(ButtonInput::Grip)] = buttons.grip.update(hand.grip).down;
    levels.down[buttonIndex(ButtonInput::StickClick)] = hand.stickClick;
    levels.down[buttonIndex(ButtonInput::Primary)] = hand.primaryButton;
    levels.down[buttonIndex(ButtonInput::Secondary)] = hand.secondaryButton;
    levels.down[buttonIndex(ButtonInput::Menu)] = hand.menuButton;
    for (std::size_t i = 0; i < kButtonInputCount; ++i) {
        const TapHoldOutput tapHold = buttons.tapHold[i].update(levels.down[i], dtSeconds);
        levels.tap[i] = tapHold.tap;
        levels.hold[i] = tapHold.hold;
    }
    return levels;
}

void InputMapper::addButtonActions(Hand hand,
                                   const ButtonLevels& levels,
                                   bool gripSuppressed,
                                   bool triggerSuppressed,
                                   game::GameActionSet& down) const {
    for (const ButtonBinding& binding : profile_.buttons) {
        if (binding.hand != hand) {
            continue;
        }
        if ((binding.input == ButtonInput::Grip && gripSuppressed) ||
            (binding.input == ButtonInput::Trigger && triggerSuppressed)) {
            continue;
        }
        const std::size_t i = buttonIndex(binding.input);
        if (bindingActive(binding.kind, levels.down[i], levels.tap[i], levels.hold[i])) {
            game::add(down, binding.action);
        }
    }
}

void InputMapper::addStickGestureActions(const TurnStickOutput& gestures, game::GameActionSet& down) const {
    for (const StickGestureBinding& binding : profile_.stickGestures) {
        if (gestureActive(binding.gesture, gestures)) {
            game::add(down, binding.action);
        }
    }
}

} // namespace evr::input
