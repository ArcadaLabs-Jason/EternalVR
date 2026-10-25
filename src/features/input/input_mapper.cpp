#include "features/input/input_mapper.hpp"

#include "common/finite.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>

namespace evr::input {

namespace {

constexpr std::size_t handIndex(Hand hand) {
    return static_cast<std::size_t>(hand);
}

constexpr std::size_t buttonIndex(ButtonInput input) {
    return static_cast<std::size_t>(input);
}

const char* sideName(Hand hand) {
    return hand == Hand::Left ? "left" : "right";
}

float sanitizedDt(float dtSeconds) {
    return std::isfinite(dtSeconds) && dtSeconds > 0.0f ? std::min(dtSeconds, kMaxFrameSeconds) : 0.0f;
}

// Policies that take their settings as a whole sanitise them in their own constructors; these are
// the parts the mapper owns or combines.
MapperSettings sanitized(MapperSettings settings, const BindingProfile& profile) {
    settings.trigger = sanitizedThresholds(settings.trigger, kTriggerThresholds);
    settings.grip = sanitizedThresholds(settings.grip, kGripThresholds);
    settings.move = sanitizedResponse(settings.move, kMoveStickResponse);
    settings.buttonHoldSeconds = TapHoldDetector(settings.buttonHoldSeconds).holdSeconds();
    settings.turnStick = TurnStickArbiter(settings.turnStick).settings();
    settings.wheelStickHoldSeconds =
        finiteInRangeOr(settings.wheelStickHoldSeconds, 0.0f, kMaxHoldSeconds, kWheelStickHoldSeconds);
    if (downHoldOnlyOpensWheel(profile)) {
        settings.turnStick.earlyHoldSeconds = settings.wheelStickHoldSeconds;
    }
    settings.turn = TurnPolicy(settings.turn).settings();
    // A sweep only starts turning once the arbiter has claimed it as a turn, so smooth turning ramps
    // up from zero at the claim deflection. Measured from the response deadzone instead, the rate
    // would jump from nothing to a sizeable fraction of full speed the moment the claim is made.
    settings.turn.smoothResponse = startingFrom(settings.turn.smoothResponse, settings.turnStick.turnClaim);
    settings.punch = PunchDetector(settings.punch).settings();
    settings.swing = ArmGestures(settings.swing).swingSettings();
    settings.reach = ReachUse(settings.reach).settings();
    settings.flick = WristFlick(settings.flick).settings();
    // One rest, one job: a rest of the wheel's bound like a button is not the wheel's.
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        if (bindsWheelRest(profile, settings, hand)) {
            settings.restSensors[handIndex(hand)] = false;
        }
    }
    return settings;
}

// The thumb rest's touch, and what tells the thumb is elsewhere on that controller (touch_button.hpp): a
// touch sensor beside the rest (its stick's, face buttons' or trackpad's), a face button pressed, its own
// stick out.
TouchButtonSample restSample(const HandState& hand) {
    TouchButtonSample sample;
    sample.raw = hand.thumbRest;
    sample.vetoTouch = hand.stickTouch || hand.trackpadTouch || hand.primaryTouch || hand.secondaryTouch;
    sample.faceButton = hand.primaryButton || hand.secondaryButton;
    sample.stickOut = magnitude(hand.stick) > kArmDeadzone;
    return sample;
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

bool downHoldOnlyOpensWheel(const BindingProfile& profile) {
    bool wheel = false;
    for (const StickGestureBinding& binding : profile.stickGestures) {
        if (binding.gesture == StickGesture::DownHold) {
            if (binding.action != game::GameAction::WeaponWheel) {
                return false;
            }
            wheel = true;
        }
    }
    return wheel;
}

std::array<std::array<OwnControls, kControlTouchCount>, 2> MapperSettings::defaultTouchOwnControls() {
    std::array<OwnControls, kControlTouchCount> hand;
    for (std::size_t i = 0; i < kControlTouchCount; ++i) {
        hand[i] = defaultOwnControls(kControlTouches[i]);
    }
    return {hand, hand};
}

std::array<std::array<OwnControls, kControlTouchCount>, 2> MapperSettings::defaultTouchLandingControls() {
    std::array<OwnControls, kControlTouchCount> hand;
    for (std::size_t i = 0; i < kControlTouchCount; ++i) {
        hand[i] = defaultLandingControls(kControlTouches[i]);
    }
    return {hand, hand};
}

bool bindsWheelRest(const BindingProfile& profile, const MapperSettings& settings, Hand hand) {
    return bindsInput(profile, hand, ButtonInput::ThumbRest) ||
           (faceTouchIsRest(settings, hand) && (bindsInput(profile, hand, ButtonInput::PrimaryTouch) ||
                                                bindsInput(profile, hand, ButtonInput::SecondaryTouch)));
}

InputMapper::InputMapper(BindingProfile profile, MapperSettings settings)
    : profile_(std::move(profile)), settings_(sanitized(settings, profile_)), turnStick_(settings_.turnStick),
      turn_(settings_.turn), handsJump_(settings_.handsJump), armGestures_(settings_.swing),
      reachUse_(settings_.reach), wristFlick_(settings_.flick), punch_(settings_.punch),
      captureChord_(settings_.trigger, settings_.captureButtons, settings_.buttonHoldSeconds),
      stickChord_(settings_.buttonHoldSeconds),
      restWheel_(settings_.restWheel, {settings_.restSensors, profile_.moveStick, profile_.turnStick}),
      menuRelease_({settings_.trigger.release, settings_.grip.release, settings_.turnStick.centreRadius}) {
    for (HandButtons& hand : hands_) {
        hand.trigger = AnalogButton(settings_.trigger);
        hand.grip = AnalogButton(settings_.grip);
        hand.tapHold.fill(TapHoldDetector(settings_.buttonHoldSeconds));
        hand.tapHold[buttonIndex(ButtonInput::Menu)] =
            TapHoldDetector(settings_.buttonHoldSeconds, settings_.menuTapSeconds);
        hand.thumbRest = TouchButton(settings_.buttonHoldSeconds, restWheel_.settings().holdOverSeconds);
        hand.touches.fill(TouchButton(settings_.buttonHoldSeconds, restWheel_.settings().holdOverSeconds,
                                      TouchKind::Control));
        // A mapper built in play (after the headset's dashboard, a gap in controller input, or another
        // family's controllers) may find a thumb or finger already on a sensor: that touch was not begun for
        // this mapper, so it presses nothing until it lifts and lands again.
        hand.thumbRest.startCancelled();
        for (TouchButton& touch : hand.touches) {
            touch.startCancelled();
        }
    }
}

GameInput InputMapper::update(const InputFrame& raw, const MapperContext& context, float dtSeconds) {
    const float dt = sanitizedDt(dtSeconds);
    GameInput input;

    // Inputs held through a menu's hold stay out of gameplay until let go, and the presses and sweeps begun
    // under it are used up: a release or a stick still out finishes none of them (menu_release_latch.hpp).
    const MenuReleaseOutput released = menuRelease_.update(raw, context.menuHold);
    if (released.holdEnded) {
        consumeHeld();
    }

    // Left Menu (or where the runtime keeps it, both sticks) held + a trigger pulled: the capture. That Menu
    // press neither taps nor holds, that stick chord does not recenter, and the trigger is held back
    // meanwhile (capture_chord.hpp).
    const CaptureChordOutput chord = captureChord_.update(released.frame, dt);
    input.capture = chord.capture;
    if (chord.cancelMenu) {
        hands_[handIndex(Hand::Left)].tapHold[buttonIndex(ButtonInput::Menu)].cancel();
    }

    // Both sticks pressed: the recenter chord. The sticks' own bindings see a click only when it is not
    // part of the chord (a single click stays instant; stick_chord.hpp).
    InputFrame frame = released.frame;
    const StickChordOutput sticks = stickChord_.update(frame.left.stickClick, frame.right.stickClick, dt);
    frame.left.stickClick = sticks.click[handIndex(Hand::Left)];
    frame.right.stickClick = sticks.click[handIndex(Hand::Right)];
    if (settings_.stickChordRecenter && sticks.recenter && !chord.cancelSticks) {
        game::add(input.down, game::GameAction::Recenter);
    }

    // The reach and the wrist flick start nothing while the game holds such input back (the thumb-rest
    // wheel's own reasons: it suppresses buttons, forces the view, plays a cutscene, a demon is piloted),
    // while a menu holds gameplay back, or while the weapon wheel is up (its pointer may be the weapon hand).
    // Each starts over afterwards.
    const bool gesturesBlocked = context.menuHold || context.restWheelBlocked ||
                                 game::contains(previousDown_, game::GameAction::WeaponWheel);
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        HandButtons& buttons = hands_[handIndex(hand)];
        const ButtonLevels levels =
            sampleButtons(buttons, frame.hand(hand), settings_.touchOwnControls[handIndex(hand)],
                          settings_.touchLandingControls[handIndex(hand)], dt);
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
    // The thumb-rest wheel decides before the turn: on the frame a stick leaves the centre it is the wheel's
    // or it is not, so a flick that picks a weapon never snaps or turns first.
    input.restWheel = updateRestWheel(frame, context, wheelFromButton, dt);
    const RestWheelOutput& rest = input.restWheel;
    const bool restTakesTurn = profile_.turnStick && rest.taken[handIndex(*profile_.turnStick)];
    const Axis2 turnStick = profile_.turnStick ? frame.hand(*profile_.turnStick).stick : Axis2{};
    // The virtual gamepad's look reads it (no turn hook): nothing while the thumb-rest wheel holds the stick.
    turnStickRead_ = restTakesTurn ? Axis2{} : turnStick;
    // A push the thumb-rest wheel holds while a rest's touch decides is still centred to the gestures and the
    // turn: given back to the game it claims, turns or snaps as a fresh push.
    // A held push short of its furthest point when the game gets it is played once from that point.
    const bool restDefersTurn = profile_.turnStick && rest.deferred[handIndex(*profile_.turnStick)];
    const std::optional<Axis2> replay =
        profile_.turnStick ? rest.replay[handIndex(*profile_.turnStick)] : std::nullopt;
    const Axis2 gestureStick = replay ? *replay : restDefersTurn ? Axis2{} : turnStick;
    const TurnStickOutput gestures =
        turnStick_.update(gestureStick, dt, wheelFromButton || (restTakesTurn && !restDefersTurn));
    addStickGestureActions(gestures, input.down);
    input.turnDegrees = turn_.update(gestureStick, dt, gestures.turnAllowed);
    input.wheelPointer = restTakesTurn && !wheelFromButton ? Axis2{} : gestures.wheelPointer;
    if (rest.wheelDown) {
        game::add(input.down, game::GameAction::WeaponWheel);
        input.wheelPointer = rest.pointer;
    }
    if (rest.slot) {
        game::add(input.down, *rest.slot);
    }

    // The hand the locomotion frame follows (the head's frame does not read it).
    const Hand moveStickHand = locomotionHand(profile_);
    const Hand steering =
        locomotionFrameHand(settings_.locomotionFrame, moveStickHand).value_or(moveStickHand);
    const float locomotionYaw =
        locomotion_.update(settings_.locomotionFrame, frame.head, frame.hand(steering));
    const bool restTakesMove = profile_.moveStick && rest.taken[handIndex(*profile_.moveStick)];
    // A push held while a rest's touch decided and given to the game short of its furthest point moves from
    // that point for one frame, as the turn stick's turns: a tap of the move stick during a hold-over is one
    // game frame of movement once the touch decides, up to a hold-over late.
    const std::optional<Axis2> moveReplay =
        profile_.moveStick ? rest.replay[handIndex(*profile_.moveStick)] : std::nullopt;
    const Axis2 moveStick = moveReplay                             ? *moveReplay
                            : profile_.moveStick && !restTakesMove ? frame.hand(*profile_.moveStick).stick
                                                                   : Axis2{};
    const Axis2 move = applyStickResponse(moveStick, settings_.move);
    input.move = rotateIntoViewFrame(move, locomotionYaw, context.viewYawRadians);

    input.handsJumped = handsJump_.update(frame, context.posture, dt);
    if (input.handsJumped) {
        game::add(input.down, game::GameAction::Jump);
    }
    // An overhead swing holds back the weapon hand's punch: the gesture's own motion would punch too.
    const ArmGestureOutput arms = armGestures_.update(frame, profile_.weaponHand, dt);
    if (arms.swung) {
        game::add(input.down, game::GameAction::Crucible);
    }
    const bool wheelUp = gesturesBlocked || game::contains(input.down, game::GameAction::WeaponWheel);
    // A reach out and back presses use alone (the game uses what is in focus; reach_use.hpp). A hand primed
    // for a swing is not reaching.
    const ReachOutput reach = reachUse_.update(frame, dt, arms.heldBack, wheelUp);
    if (reach.useDown) {
        game::add(input.down, game::GameAction::Use);
    }
    input.reached = reach.reached;
    // A flick of the weapon hand's wrist switches the weapon mod (wrist_flick.hpp).
    const FlickOutput flick = wristFlick_.update(frame, profile_.weaponHand, dt, wheelUp);
    if (flick.flicked) {
        game::add(input.down, game::GameAction::SwitchWeaponMod);
    }
    input.flicked = flick.flicked;
    input.swung = arms.swung;
    if (punch_.update(frame, arms.heldBack)) {
        input.meleeFromPunch = !game::contains(input.down, game::GameAction::Melee) &&
                               !game::contains(previousDown_, game::GameAction::Melee);
        game::add(input.down, game::GameAction::Melee);
        input.punch = punch_.punched();
    }

    input.pressed = input.down & ~previousDown_;
    input.released = previousDown_ & ~input.down;
    previousDown_ = input.down;
    return input;
}

std::string InputMapper::summary() const {
    std::string text = std::string(sideName(profile_.weaponHand)) + "-handed, move stick ";
    text += profile_.moveStick ? sideName(*profile_.moveStick) : "none";
    const std::optional<Hand> steering =
        locomotionFrameHand(settings_.locomotionFrame, locomotionHand(profile_));
    text += steering ? std::string(", moving where the ") + sideName(*steering) + " hand points"
                     : ", moving where the head faces";
    return text;
}

InputMapper::ButtonLevels
InputMapper::sampleButtons(HandButtons& buttons,
                           const HandState& hand,
                           const std::array<OwnControls, kControlTouchCount>& own,
                           const std::array<OwnControls, kControlTouchCount>& landing,
                           float dtSeconds) {
    ButtonLevels levels;
    levels.down[buttonIndex(ButtonInput::Trigger)] = buttons.trigger.update(hand.trigger).down;
    levels.down[buttonIndex(ButtonInput::Grip)] = buttons.grip.update(hand.grip).down;
    levels.down[buttonIndex(ButtonInput::StickClick)] = hand.stickClick;
    levels.down[buttonIndex(ButtonInput::Primary)] = hand.primaryButton;
    levels.down[buttonIndex(ButtonInput::Secondary)] = hand.secondaryButton;
    levels.down[buttonIndex(ButtonInput::Face3)] = hand.face3Button;
    levels.down[buttonIndex(ButtonInput::Face4)] = hand.face4Button;
    levels.down[buttonIndex(ButtonInput::Shoulder)] = hand.shoulderButton;
    levels.down[buttonIndex(ButtonInput::Menu)] = hand.menuButton;
    for (std::size_t i = 0; i < kButtonInputCount; ++i) {
        const TapHoldOutput tapHold = buttons.tapHold[i].update(levels.down[i], dtSeconds);
        levels.tap[i] = tapHold.tap;
        levels.hold[i] = tapHold.hold;
    }
    // The thumb rest is a touch: its own filter, tap and hold (touch_button.hpp).
    const TouchButtonOutput rest = buttons.thumbRest.update(restSample(hand), dtSeconds);
    const std::size_t r = buttonIndex(ButtonInput::ThumbRest);
    levels.down[r] = rest.down;
    levels.tap[r] = rest.tap;
    levels.hold[r] = rest.hold;
    // A touch on a control: its own filter too, and its own control in use ends it (touch_button.hpp).
    const OwnControlLevels analog{levels.down[buttonIndex(ButtonInput::Trigger)],
                                  levels.down[buttonIndex(ButtonInput::Grip)]};
    for (std::size_t t = 0; t < kControlTouchCount; ++t) {
        const ButtonInput input = kControlTouches[t];
        TouchButtonSample sample;
        sample.raw = touchSensed(hand, input);
        sample.ownControl = ownControlUsed(hand, own[t], analog);
        sample.ownLanding = ownControlUsed(hand, landing[t], analog);
        const TouchButtonOutput touch = buttons.touches[t].update(sample, dtSeconds);
        const std::size_t i = buttonIndex(input);
        levels.down[i] = touch.down;
        levels.tap[i] = touch.tap;
        levels.hold[i] = touch.hold;
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

RestWheelOutput InputMapper::updateRestWheel(const InputFrame& frame,
                                             const MapperContext& context,
                                             bool wheelFromButton,
                                             float dt) {
    RestWheelFrame rest;
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        const HandState& h = frame.hand(hand);
        const bool faceRest = faceTouchIsRest(settings_, hand);
        const bool pressed = h.primaryButton || h.secondaryButton;
        // A face button counts while touched and not pressed: a thumb pressing jump is not resting.
        const bool face =
            faceRest && ((h.primaryTouch && !h.primaryButton) || (h.secondaryTouch && !h.secondaryButton));
        rest.rest[handIndex(hand)] = h.thumbRest || face;
        // A touch sensor says the thumb is on its own stick, face buttons or trackpad: a touch begun there is
        // not a landing yet, and a touch lost after it went there lifts at once (rest_wheel.hpp; a press and
        // the stick out of the centre count there too). Where a face button's touch is the rest, it is not
        // one.
        rest.thumbElsewhere[handIndex(hand)] =
            h.stickTouch || h.trackpadTouch || (!faceRest && (h.primaryTouch || h.secondaryTouch));
        rest.sticks[handIndex(hand)] = h.stick;
        rest.faceButtons[handIndex(hand)] = pressed;
    }
    rest.minWheelHoldSeconds = context.restWheelHoldSeconds;
    // Another route holding the wheel (a button, or the turn stick's own down sweep) keeps it; a down sweep
    // begun with a rest's touch is taken back while the touch registers (RestWheel holds the stick).
    rest.blocked = context.menuHold || context.restWheelBlocked || wheelFromButton;
    rest.sweepDown = turnStick_.intent() == SweepIntent::Down;
    return restWheel_.update(rest, dt);
}

void InputMapper::consumeHeld() {
    // Only presses going on: a button up until now may be pressed afresh on this very frame.
    for (HandButtons& hand : hands_) {
        for (TapHoldDetector& button : hand.tapHold) {
            if (button.isDown()) {
                button.cancel();
            }
        }
        hand.thumbRest.cancel();
        for (TouchButton& touch : hand.touches) {
            touch.cancel();
        }
    }
    turnStick_.cancelSweep();
    restWheel_.cancel();
    // A stick click withheld for the chord would be sent late on its release.
    stickChord_ = StickChord(settings_.buttonHoldSeconds);
}

void InputMapper::addStickGestureActions(const TurnStickOutput& gestures, game::GameActionSet& down) const {
    for (const StickGestureBinding& binding : profile_.stickGestures) {
        if (gestureActive(binding.gesture, gestures)) {
            game::add(down, binding.action);
        }
    }
}

} // namespace evr::input
