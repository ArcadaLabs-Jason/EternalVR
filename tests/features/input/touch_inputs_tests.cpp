// The touches on controls a control map can bind (touch_inputs.hpp): where each family has the sensor, the
// controls under it, and their OpenXR bindings, added only when a map binds one (rest_touch_bindings.hpp).

#include "features/input/touch_inputs.hpp"

#include "features/input/controller_bindings.hpp"
#include "features/input/family_touch_settings.hpp"
#include "features/input/rest_touch_bindings.hpp"
#include "game/eternal/controller_data.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <string>

using evr::game::builtinControllerData;
using evr::game::Controller;
using evr::game::Handedness;
using evr::game::kControllers;
using evr::input::addBoundTouchBindings;
using evr::input::addRestTouchBindings;
using evr::input::ButtonInput;
using evr::input::ControllerData;
using evr::input::Hand;
using evr::input::kControlTouches;
using evr::input::OwnControls;
using evr::input::ownControlsOf;
using evr::input::parseControllerData;
using evr::input::restTouchOptions;
using evr::input::touchLeaf;
using evr::input::TouchRetry;
using evr::input::XrActionId;

namespace {

ControllerData builtin(Controller controller) {
    return parseControllerData(builtinControllerData(controller));
}

std::string leaf(Controller controller, Hand hand, ButtonInput touch) {
    return touchLeaf(builtin(controller), hand, touch).value_or("");
}

std::string pathOf(const ControllerData& data, XrActionId action, Hand hand) {
    const auto* binding = data.find(action, hand);
    return binding ? binding->path : std::string{};
}

OwnControls own(std::initializer_list<XrActionId> actions) {
    OwnControls o;
    for (const XrActionId a : actions) {
        o.add(a);
    }
    return o;
}

// `data` as the layer has it for a session with the wheel off: what a map binds added, nothing else.
ControllerData session(ControllerData data) {
    addRestTouchBindings(data, restTouchOptions(data, false, false));
    addBoundTouchBindings(data);
    return data;
}

} // namespace

TEST_CASE("the touch inputs are the thumb rest and the five touches on controls") {
    CHECK(kControlTouches.size() == 5);
    for (const ButtonInput touch : kControlTouches) {
        CHECK(evr::input::isControlTouch(touch));
        CHECK(evr::input::isTouchInput(touch));
        CHECK(kControlTouches[evr::input::controlTouchIndex(touch)] == touch);
        CHECK(evr::input::touchAction(touch).has_value());
    }
    CHECK(evr::input::isTouchInput(ButtonInput::ThumbRest));
    CHECK_FALSE(evr::input::isControlTouch(ButtonInput::ThumbRest));
    CHECK_FALSE(evr::input::isTouchInput(ButtonInput::Primary));
    CHECK_FALSE(evr::input::touchAction(ButtonInput::Menu).has_value());
    CHECK(evr::input::touchAction(ButtonInput::TriggerTouch) == XrActionId::TriggerTouch);
    CHECK(evr::input::touchAction(ButtonInput::StickTouch) == XrActionId::ThumbstickTouch);
}

TEST_CASE("each family offers a touch only where its profile has the sensor") {
    // Touch: X/Y and A/B, the stick, the trigger; no trackpad.
    CHECK(leaf(Controller::OculusTouch, Hand::Left, ButtonInput::PrimaryTouch) == "/input/x/touch");
    CHECK(leaf(Controller::OculusTouch, Hand::Left, ButtonInput::SecondaryTouch) == "/input/y/touch");
    CHECK(leaf(Controller::OculusTouch, Hand::Right, ButtonInput::PrimaryTouch) == "/input/a/touch");
    CHECK(leaf(Controller::OculusTouch, Hand::Right, ButtonInput::StickTouch) == "/input/thumbstick/touch");
    CHECK(leaf(Controller::OculusTouch, Hand::Right, ButtonInput::TriggerTouch) == "/input/trigger/touch");
    CHECK(leaf(Controller::OculusTouch, Hand::Right, ButtonInput::TrackpadTouch).empty());
    // Index: all five on both hands.
    for (const ButtonInput touch : kControlTouches) {
        CHECK_FALSE(leaf(Controller::ValveIndex, Hand::Left, touch).empty());
        CHECK_FALSE(leaf(Controller::ValveIndex, Hand::Right, touch).empty());
    }
    CHECK(leaf(Controller::ValveIndex, Hand::Left, ButtonInput::TrackpadTouch) == "/input/trackpad/touch");
    // G2: none.
    for (const ButtonInput touch : kControlTouches) {
        CHECK(leaf(Controller::HpReverbG2, Hand::Right, touch).empty());
    }
    // WMR: the trackpad only; its primary button is the trackpad's click, which has no face-button touch.
    CHECK(leaf(Controller::WindowsMixedReality, Hand::Left, ButtonInput::TrackpadTouch) ==
          "/input/trackpad/touch");
    CHECK(leaf(Controller::WindowsMixedReality, Hand::Left, ButtonInput::PrimaryTouch).empty());
    CHECK(leaf(Controller::WindowsMixedReality, Hand::Left, ButtonInput::StickTouch).empty());
    // Cosmos: the stick only.
    CHECK(leaf(Controller::ViveCosmos, Hand::Left, ButtonInput::StickTouch) == "/input/thumbstick/touch");
    CHECK(leaf(Controller::ViveCosmos, Hand::Left, ButtonInput::PrimaryTouch).empty());
    CHECK(leaf(Controller::ViveCosmos, Hand::Left, ButtonInput::TriggerTouch).empty());
    // Vive wands: the trackpad only.
    CHECK(leaf(Controller::ViveWand, Hand::Right, ButtonInput::TrackpadTouch) == "/input/trackpad/touch");
    CHECK(leaf(Controller::ViveWand, Hand::Right, ButtonInput::StickTouch).empty());
    // Pico 4: face buttons, stick, trigger.
    CHECK(leaf(Controller::Pico4, Hand::Left, ButtonInput::SecondaryTouch) == "/input/y/touch");
    CHECK(leaf(Controller::Pico4, Hand::Right, ButtonInput::TriggerTouch) == "/input/trigger/touch");
    CHECK(leaf(Controller::Pico4, Hand::Right, ButtonInput::TrackpadTouch).empty());
    // Steam Frame: A/B on the right; the left face buttons are a D-pad, which has no A/B/X/Y touch.
    CHECK(leaf(Controller::SteamFrame, Hand::Right, ButtonInput::PrimaryTouch) == "/input/a/touch");
    CHECK(leaf(Controller::SteamFrame, Hand::Left, ButtonInput::PrimaryTouch).empty());
    CHECK(leaf(Controller::SteamFrame, Hand::Left, ButtonInput::StickTouch) == "/input/thumbstick/touch");
    CHECK(leaf(Controller::SteamFrame, Hand::Left, ButtonInput::TriggerTouch) == "/input/trigger/touch");
}

TEST_CASE("the controls under a touch come from the family's data") {
    // A thumb's touch: every control under that thumb, the same for A/X, B/Y, the stick and the trackpad.
    const ControllerData touch = builtin(Controller::OculusTouch);
    const OwnControls touchRight = own(
        {XrActionId::Primary, XrActionId::Secondary, XrActionId::Thumbstick, XrActionId::ThumbstickClick});
    for (const ButtonInput t : {ButtonInput::PrimaryTouch, ButtonInput::SecondaryTouch,
                                ButtonInput::StickTouch, ButtonInput::TrackpadTouch}) {
        const int which = static_cast<int>(t);
        CAPTURE(which);
        CHECK(ownControlsOf(touch, Hand::Right, t) == touchRight);
        // The left Menu button is under the left thumb too.
        CHECK(ownControlsOf(touch, Hand::Left, t) ==
              own({XrActionId::Primary, XrActionId::Secondary, XrActionId::Thumbstick,
                   XrActionId::ThumbstickClick, XrActionId::Menu}));
    }
    // The trigger's touch: the trigger only.
    CHECK(ownControlsOf(touch, Hand::Right, ButtonInput::TriggerTouch) == own({XrActionId::Trigger}));
    // The Steam Frame: A, B, X, Y and Menu on the right, the D-pad and View on the left; not the bumper.
    const ControllerData frame = builtin(Controller::SteamFrame);
    const OwnControls frameFace =
        own({XrActionId::Primary, XrActionId::Secondary, XrActionId::Face3, XrActionId::Face4,
             XrActionId::Thumbstick, XrActionId::ThumbstickClick, XrActionId::Menu});
    CHECK(ownControlsOf(frame, Hand::Right, ButtonInput::PrimaryTouch) == frameFace);
    CHECK(ownControlsOf(frame, Hand::Right, ButtonInput::SecondaryTouch) == frameFace);
    CHECK(ownControlsOf(frame, Hand::Left, ButtonInput::StickTouch) == frameFace);
    CHECK(ownControlsOf(frame, Hand::Right, ButtonInput::TriggerTouch) == own({XrActionId::Trigger}));
    // The Vive wands' stick is the trackpad; the right secondary is the Menu button above it.
    CHECK(ownControlsOf(builtin(Controller::ViveWand), Hand::Left, ButtonInput::TrackpadTouch) ==
          own({XrActionId::Thumbstick, XrActionId::ThumbstickClick, XrActionId::Menu}));
    CHECK(ownControlsOf(builtin(Controller::ViveWand), Hand::Right, ButtonInput::TrackpadTouch) ==
          own({XrActionId::Thumbstick, XrActionId::ThumbstickClick, XrActionId::Secondary}));
    // Index: the left Menu button is the trackpad pressed hard.
    const ControllerData index = builtin(Controller::ValveIndex);
    CHECK(ownControlsOf(index, Hand::Left, ButtonInput::TrackpadTouch) ==
          own({XrActionId::Primary, XrActionId::Secondary, XrActionId::Thumbstick,
               XrActionId::ThumbstickClick, XrActionId::Menu}));
    CHECK(ownControlsOf(index, Hand::Right, ButtonInput::TrackpadTouch) == touchRight);
    // WMR: the primary button is the trackpad's click, the right secondary its Menu button.
    CHECK(ownControlsOf(builtin(Controller::WindowsMixedReality), Hand::Right, ButtonInput::TrackpadTouch) ==
          touchRight);
    // No such sensor: the family's controls under the thumb all the same (the key does nothing there).
    CHECK(ownControlsOf(builtin(Controller::HpReverbG2), Hand::Right, ButtonInput::PrimaryTouch) ==
          touchRight);
    CHECK(ownControlsOf(builtin(Controller::HpReverbG2), Hand::Right, ButtonInput::TriggerTouch) ==
          own({XrActionId::Trigger}));
    // The plain layout's default.
    CHECK(evr::input::defaultOwnControls(ButtonInput::TrackpadTouch) == touchRight);
    CHECK(evr::input::defaultOwnControls(ButtonInput::TriggerTouch) == own({XrActionId::Trigger}));
}

TEST_CASE("the controls whose use voids a landing are those under the touch itself") {
    using evr::input::landingControlsOf;
    const ControllerData touch = builtin(Controller::OculusTouch);
    CHECK(landingControlsOf(touch, Hand::Right, ButtonInput::PrimaryTouch) == own({XrActionId::Primary}));
    CHECK(landingControlsOf(touch, Hand::Left, ButtonInput::SecondaryTouch) == own({XrActionId::Secondary}));
    CHECK(landingControlsOf(touch, Hand::Left, ButtonInput::StickTouch) ==
          own({XrActionId::Thumbstick, XrActionId::ThumbstickClick}));
    CHECK(landingControlsOf(touch, Hand::Right, ButtonInput::TriggerTouch) == own({XrActionId::Trigger}));
    CHECK(landingControlsOf(builtin(Controller::ViveWand), Hand::Left, ButtonInput::TrackpadTouch) ==
          own({XrActionId::Thumbstick, XrActionId::ThumbstickClick}));
    CHECK(landingControlsOf(builtin(Controller::ValveIndex), Hand::Left, ButtonInput::TrackpadTouch) ==
          own({XrActionId::Menu}));
    CHECK(landingControlsOf(builtin(Controller::SteamFrame), Hand::Right, ButtonInput::SecondaryTouch) ==
          own({XrActionId::Secondary}));
    // No such sensor: the plain layout's (nothing for the trackpad).
    CHECK(landingControlsOf(builtin(Controller::HpReverbG2), Hand::Right, ButtonInput::PrimaryTouch) ==
          own({XrActionId::Primary}));
    CHECK(evr::input::defaultLandingControls(ButtonInput::TrackpadTouch).empty());
}

TEST_CASE("an own control counts in use when pressed, pushed out or pulled") {
    using evr::input::OwnControlLevels;
    using evr::input::ownControlUsed;
    evr::input::HandState h;
    CHECK_FALSE(
        ownControlUsed(h, own({XrActionId::Primary, XrActionId::Thumbstick, XrActionId::Trigger}), {}));
    h.primaryButton = true;
    CHECK(ownControlUsed(h, own({XrActionId::Primary}), {}));
    CHECK_FALSE(ownControlUsed(h, own({XrActionId::Secondary}), {}));
    h = {};
    h.stick = {0.2f, 0.0f};
    CHECK_FALSE(ownControlUsed(h, own({XrActionId::Thumbstick}), {}));
    h.stick = {0.0f, -0.3f};
    CHECK(ownControlUsed(h, own({XrActionId::Thumbstick}), {}));
    h = {};
    h.stickClick = true;
    CHECK(ownControlUsed(h, own({XrActionId::ThumbstickClick}), {}));
    h = {};
    CHECK(ownControlUsed(h, own({XrActionId::Trigger}), OwnControlLevels{true, false}));
    CHECK_FALSE(ownControlUsed(h, own({XrActionId::Trigger}), OwnControlLevels{false, true}));
    h.menuButton = true;
    CHECK(ownControlUsed(h, own({XrActionId::Menu}), {}));
}

TEST_CASE("default guard: no touch bound, no touch binding added, on any family") {
    for (const Controller controller : kControllers) {
        CAPTURE(evr::game::controllerName(controller));
        const ControllerData data = builtin(controller);
        ControllerData added = data;
        CHECK(addBoundTouchBindings(added) == 0);
        CHECK(added.suggested == data.suggested);
        CHECK(session(data).suggested == data.suggested);
        for (const ButtonInput touch : kControlTouches) {
            CHECK_FALSE(evr::input::bindsTouch(data, Hand::Left, touch));
            CHECK_FALSE(evr::input::bindsTouch(data, Hand::Right, touch));
        }
        // "none" binds nothing.
        ControllerData unbound = data;
        unbound.maps[Handedness::Right]["right.trigger_touch.tap"] = "none";
        CHECK(addBoundTouchBindings(unbound) == 0);
    }
}

TEST_CASE("default guard: the wheel and face touch on, no touch key: the bound touches add nothing") {
    for (const Controller controller : kControllers) {
        CAPTURE(evr::game::controllerName(controller));
        ControllerData data = builtin(controller);
        addRestTouchBindings(data, restTouchOptions(data, true, true));
        ControllerData added = data;
        CHECK(addBoundTouchBindings(added) == 0);
        CHECK(added.suggested == data.suggested);
        CHECK_FALSE(evr::input::hasTouchButtonBindings(data));
        for (const auto& step : evr::input::touchRetries(data)) {
            CHECK(step.retry != TouchRetry::WithoutTouchButtons);
        }
    }
}

TEST_CASE("a bound touch gets its binding where the family has the sensor, the Steam Frame too") {
    ControllerData touch = builtin(Controller::OculusTouch);
    touch.maps[Handedness::Right]["right.primary_touch.tap"] = "weapon_slot_2";
    touch.maps[Handedness::LeftButtonSwap]["left.trigger_touch.hold"] = "automap";
    const ControllerData s = session(touch);
    CHECK(s.suggested.size() == builtin(Controller::OculusTouch).suggested.size() + 2);
    CHECK(pathOf(s, XrActionId::PrimaryTouch, Hand::Right) == "/user/hand/right/input/a/touch");
    CHECK(pathOf(s, XrActionId::TriggerTouch, Hand::Left) == "/user/hand/left/input/trigger/touch");
    // Nothing else of the wheel's: no stick touch, no left face touch.
    CHECK(s.find(XrActionId::ThumbstickTouch, Hand::Right) == nullptr);
    CHECK(s.find(XrActionId::PrimaryTouch, Hand::Left) == nullptr);
    // Twice adds nothing more.
    ControllerData again = s;
    CHECK(addBoundTouchBindings(again) == 0);

    ControllerData frame = builtin(Controller::SteamFrame);
    frame.maps[Handedness::Right]["right.secondary_touch.press"] = "jump";
    frame.maps[Handedness::Right]["left.stick_touch.tap"] = "quick_switch";
    // The left face buttons are a D-pad: no face-button touch there, so the key adds nothing.
    frame.maps[Handedness::Right]["left.primary_touch.tap"] = "dash";
    CHECK(addBoundTouchBindings(frame) == 2);
    CHECK(pathOf(frame, XrActionId::SecondaryTouch, Hand::Right) == "/user/hand/right/input/b/touch");
    CHECK(pathOf(frame, XrActionId::ThumbstickTouch, Hand::Left) == "/user/hand/left/input/thumbstick/touch");
    CHECK(frame.find(XrActionId::PrimaryTouch, Hand::Left) == nullptr);

    // A family without the sensor: nothing (the G2 has no touch sensors).
    ControllerData g2 = builtin(Controller::HpReverbG2);
    g2.maps[Handedness::Right]["right.trigger_touch.tap"] = "dash";
    CHECK(addBoundTouchBindings(g2) == 0);
}

TEST_CASE("a refused profile keeps a bound touch on the first retry and drops it on the next") {
    ControllerData touch = builtin(Controller::OculusTouch);
    touch.maps[Handedness::Right]["right.stick_touch.tap"] = "quick_switch";
    // The wheel on: the stick touches are the wheel's veto sensors too.
    addRestTouchBindings(touch, restTouchOptions(touch, true, false));
    addBoundTouchBindings(touch);
    REQUIRE(touch.find(XrActionId::ThumbstickTouch, Hand::Right) != nullptr);
    REQUIRE(touch.find(XrActionId::ThumbstickTouch, Hand::Left) != nullptr);
    const ControllerData first = evr::input::withoutVetoTouch(touch);
    CHECK(first.find(XrActionId::ThumbstickTouch, Hand::Right) != nullptr); // bound: kept
    CHECK(first.find(XrActionId::ThumbstickTouch, Hand::Left) == nullptr);  // only the wheel's: dropped
    CHECK(first.find(XrActionId::PrimaryTouch, Hand::Right) == nullptr);
    CHECK_FALSE(evr::input::hasVetoTouchBindings(first));
    const ControllerData second = evr::input::withoutRestTouch(first);
    CHECK(second.find(XrActionId::ThumbstickTouch, Hand::Right) == nullptr);
    CHECK(evr::input::isRestTouchAction(XrActionId::TriggerTouch));
}

TEST_CASE("a refused profile drops a bound touch before the rests") {
    // The wheel on, A touched bound: the veto sensors go first, then the bound touch with the rests kept,
    // then the rests.
    ControllerData touch = builtin(Controller::OculusTouch);
    touch.maps[Handedness::Right]["right.primary_touch.tap"] = "weapon_slot_2";
    addRestTouchBindings(touch, restTouchOptions(touch, true, false));
    addBoundTouchBindings(touch);
    REQUIRE(touch.find(XrActionId::PrimaryTouch, Hand::Right) != nullptr);
    const auto steps = evr::input::touchRetries(touch);
    REQUIRE(steps.size() == 3);
    CHECK(steps[0].retry == TouchRetry::WithoutVeto);
    CHECK(steps[0].data.find(XrActionId::PrimaryTouch, Hand::Right) != nullptr);
    CHECK(steps[0].data.find(XrActionId::ThumbstickTouch, Hand::Right) == nullptr);
    CHECK(steps[1].retry == TouchRetry::WithoutTouchButtons);
    CHECK(steps[1].data.find(XrActionId::PrimaryTouch, Hand::Right) == nullptr);
    CHECK(steps[1].data.find(XrActionId::ThumbRest, Hand::Left) != nullptr);
    CHECK(steps[1].data.find(XrActionId::ThumbRest, Hand::Right) != nullptr);
    CHECK_FALSE(evr::input::hasTouchButtonBindings(steps[1].data));
    CHECK(steps[2].retry == TouchRetry::WithoutRestTouch);
    CHECK_FALSE(evr::input::hasRestTouchBindings(steps[2].data));

    // No touch bound: the two steps there were before.
    ControllerData plain = builtin(Controller::OculusTouch);
    addRestTouchBindings(plain, restTouchOptions(plain, true, false));
    const auto two = evr::input::touchRetries(plain);
    REQUIRE(two.size() == 2);
    CHECK(two[0].retry == TouchRetry::WithoutVeto);
    CHECK(two[1].retry == TouchRetry::WithoutRestTouch);

    // Index with the wheel off and only the trigger's touch bound: one step, which leaves out that touch.
    ControllerData index = builtin(Controller::ValveIndex);
    index.maps[Handedness::Right]["right.trigger_touch.tap"] = "quick_switch";
    addRestTouchBindings(index, restTouchOptions(index, false, false));
    addBoundTouchBindings(index);
    const auto one = evr::input::touchRetries(index);
    REQUIRE(one.size() == 1);
    CHECK(one[0].retry == TouchRetry::WithoutTouchButtons);
    CHECK(one[0].data.suggested == builtin(Controller::ValveIndex).suggested);
    // The built-in Index data with the wheel off: nothing to retry without.
    CHECK(evr::input::touchRetries(builtin(Controller::ValveIndex)).empty());
}

TEST_CASE("Face buttons as thumb rest never makes the Steam Frame's face touch a rest") {
    // A face touch bound only in a map not in use still gets its binding on the Frame; it is no rest there.
    ControllerData frame = builtin(Controller::SteamFrame);
    frame.maps[Handedness::LeftButtonAndStickSwap]["right.primary_touch.tap"] = "quick_switch";
    addRestTouchBindings(frame, restTouchOptions(frame, true, true));
    addBoundTouchBindings(frame);
    REQUIRE(frame.find(XrActionId::PrimaryTouch, Hand::Right) != nullptr);
    CHECK_FALSE(evr::input::faceTouchRests(frame, true));
    CHECK(evr::input::wheelRestHands(frame, true) == std::array<bool, 2>{false, false});
    // The rest test without the Frame's exclusion finds one (what the mapper was given before).
    CHECK(evr::input::handHasRest(frame, Hand::Right, true));

    // Index: A/B touched is each hand's rest with face touch on, none with it off.
    ControllerData index = builtin(Controller::ValveIndex);
    addRestTouchBindings(index, restTouchOptions(index, true, true));
    CHECK(evr::input::faceTouchRests(index, true));
    CHECK(evr::input::wheelRestHands(index, true) == std::array<bool, 2>{true, true});
    CHECK(evr::input::wheelRestHands(index, false) == std::array<bool, 2>{false, false});
}

TEST_CASE("the mapper's settings from a family's data: the Steam Frame's face touch is never a rest") {
    // The Frame with Face buttons as thumb rest on and the map in use binding A touched: the touch has its
    // binding, but neither hand has a rest and face touch does not count.
    ControllerData frame = builtin(Controller::SteamFrame);
    frame.maps[Handedness::Right]["right.primary_touch.tap"] = "quick_switch";
    addRestTouchBindings(frame, restTouchOptions(frame, true, true));
    addBoundTouchBindings(frame);
    REQUIRE(frame.find(XrActionId::PrimaryTouch, Hand::Right) != nullptr);
    evr::input::MapperSettings settings;
    evr::input::applyFamilyTouchSettings(settings, frame, true, false);
    CHECK_FALSE(settings.restFaceTouch);
    CHECK(settings.restSensors == std::array<bool, 2>{false, false});
    // Scripted input stands in for a rest on both hands, face touch still not counting.
    evr::input::applyFamilyTouchSettings(settings, frame, true, true);
    CHECK_FALSE(settings.restFaceTouch);
    CHECK(settings.restSensors == std::array<bool, 2>{true, true});

    // Index: A/B touched is each hand's rest (positive control).
    ControllerData index = builtin(Controller::ValveIndex);
    addRestTouchBindings(index, restTouchOptions(index, true, true));
    evr::input::MapperSettings onIndex;
    evr::input::applyFamilyTouchSettings(onIndex, index, true, false);
    CHECK(onIndex.restFaceTouch);
    CHECK(onIndex.restSensors == std::array<bool, 2>{true, true});
    CHECK(onIndex.restIsThumbRest == std::array<bool, 2>{false, false});

    // Touch: the thumb rests; a thumb's touch ends on any control under the thumb, its landing only after
    // its own button.
    evr::input::MapperSettings onTouch;
    evr::input::applyFamilyTouchSettings(onTouch, builtin(Controller::OculusTouch), false, false);
    CHECK(onTouch.restIsThumbRest == std::array<bool, 2>{true, true});
    CHECK(onTouch.restSensors == std::array<bool, 2>{true, true});
    const std::size_t right = static_cast<std::size_t>(Hand::Right);
    CHECK(onTouch.touchOwnControls[right][0] == own({XrActionId::Primary, XrActionId::Secondary,
                                                     XrActionId::Thumbstick, XrActionId::ThumbstickClick}));
    CHECK(onTouch.touchLandingControls[right][0] == own({XrActionId::Primary}));
}
