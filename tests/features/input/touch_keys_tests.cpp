// The touches on controls in binding text, the compiler, the prompts' names, the OpenXR action set and
// scripted input (touch_inputs.hpp).

#include "features/input/binding_compiler.hpp"
#include "features/input/binding_keys.hpp"
#include "features/input/binding_text.hpp"
#include "features/input/button_labels.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/rest_touch_bindings.hpp"
#include "features/input/test_input.hpp"
#include "features/input/xr_action_set.hpp"
#include "game/eternal/controller_data.hpp"

#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <vector>

using evr::game::Controller;
using evr::game::GameAction;
using evr::game::Handedness;
using evr::input::BindingConflict;
using evr::input::BindingKey;
using evr::input::buildBindingProfile;
using evr::input::ButtonBinding;
using evr::input::ButtonInput;
using evr::input::ButtonKey;
using evr::input::formatBindingKey;
using evr::input::Hand;
using evr::input::LabelInput;
using evr::input::parseBindingKey;
using evr::input::parseBindingText;
using evr::input::PressKind;
using evr::input::XrActionId;

TEST_CASE("touch keys parse into what they name and format back") {
    CHECK(parseBindingKey("right.primary_touch.tap") ==
          BindingKey{ButtonKey{Hand::Right, ButtonInput::PrimaryTouch, PressKind::Tap}});
    CHECK(parseBindingKey("left.secondary_touch.hold") ==
          BindingKey{ButtonKey{Hand::Left, ButtonInput::SecondaryTouch, PressKind::Hold}});
    CHECK(parseBindingKey("left.stick_touch.press") ==
          BindingKey{ButtonKey{Hand::Left, ButtonInput::StickTouch, PressKind::WhileDown}});
    CHECK(parseBindingKey("right.trackpad_touch.tap") ==
          BindingKey{ButtonKey{Hand::Right, ButtonInput::TrackpadTouch, PressKind::Tap}});
    CHECK(parseBindingKey("right.trigger_touch.hold") ==
          BindingKey{ButtonKey{Hand::Right, ButtonInput::TriggerTouch, PressKind::Hold}});
    CHECK(formatBindingKey(ButtonKey{Hand::Left, ButtonInput::StickTouch, PressKind::Tap}) ==
          "left.stick_touch.tap");
    // The OpenXR action names are not binding keys, nor is a touch without a press kind.
    for (const char* key : {"left.thumbstick_touch.tap", "right.a_touch.tap", "right.trigger_touch",
                            "right.touch.tap", "right.primary.touch"}) {
        CAPTURE(key);
        CHECK_FALSE(parseBindingKey(key).has_value());
    }
}

TEST_CASE("a touch binds like a button through the compiler, and a press on it rules out a tap or a hold") {
    auto result = buildBindingProfile(parseBindingText(R"(right.primary_touch.tap = "weapon_slot_2"
left.trigger_touch.hold = "automap"
right.stick_touch.tap = "quick_switch"
)")
                                          .entries);
    CHECK(result.ok());
    CHECK(result.profile.buttons ==
          std::vector<ButtonBinding>{
              {Hand::Left, ButtonInput::TriggerTouch, PressKind::Hold, GameAction::Automap},
              {Hand::Right, ButtonInput::PrimaryTouch, PressKind::Tap, GameAction::WeaponSlot2},
              {Hand::Right, ButtonInput::StickTouch, PressKind::Tap, GameAction::QuickSwitch}});
    // Back to the same entries, written in the layer's order.
    const auto map = evr::input::toBindingMap(result.profile);
    CHECK(buildBindingProfile(map).profile.buttons == result.profile.buttons);
    CHECK(map.at("right.primary_touch.tap") == "weapon_slot_2");
    const std::string text = evr::input::formatBindingText(map);
    CHECK(text.find("left.trigger_touch.hold") < text.find("right.primary_touch.tap"));

    result = buildBindingProfile(parseBindingText(R"(right.trigger_touch.press = "fire"
right.trigger_touch.tap = "dash"
)")
                                     .entries);
    CHECK_FALSE(result.ok());
    bool conflict = false;
    for (const auto& issue : result.issues) {
        conflict = conflict || (issue.conflict == BindingConflict::PressWithTapOrHold &&
                                issue.key == "right.trigger_touch.tap");
    }
    CHECK(conflict);
}

TEST_CASE("a bound touch is named for the prompts: the control and Touch") {
    using evr::input::nameFromInputPath;
    CHECK(nameFromInputPath("/input/a/touch") == "A Touch");
    CHECK(nameFromInputPath("/user/hand/left/input/thumbstick/touch") == "Stick Touch");
    CHECK(nameFromInputPath("/input/trackpad/touch") == "Trackpad Touch");
    CHECK(nameFromInputPath("/input/trigger/touch") == "Trigger Touch");
    CHECK(nameFromInputPath("/input/thumbrest/touch") == "Thumb Rest");
    CHECK(nameFromInputPath("/input/a/click") == "A");

    // Touch controllers with right A touch and the trigger touches bound: the session's data names them.
    auto data = evr::input::parseControllerData(evr::game::builtinControllerData(Controller::OculusTouch));
    data.maps[Handedness::Right]["right.primary_touch.tap"] = "weapon_slot_2";
    data.maps[Handedness::Right]["left.trigger_touch.tap"] = "dash";
    data.maps[Handedness::Right]["right.trigger_touch.tap"] = "automap";
    evr::input::addBoundTouchBindings(data);
    const auto labels = evr::input::buttonLabelsFor(data);
    CHECK(labels.name(Hand::Right, LabelInput::PrimaryTouch) == "A Touch");
    CHECK(labels.name(Hand::Right, LabelInput::TriggerTouch) == "Right Trigger Touch");
    CHECK(labels.name(Hand::Left, LabelInput::TriggerTouch) == "Left Trigger Touch");
    CHECK(labels.name(Hand::Right, LabelInput::StickTouch).empty()); // not bound, not in the data
    CHECK(evr::input::parseLabelKey("right.trigger_touch") ==
          evr::input::LabelKey{Hand::Right, LabelInput::TriggerTouch});
    CHECK(evr::input::formatLabelKey({Hand::Left, LabelInput::StickTouch}) == "left.stick_touch");
    const auto profile = buildBindingProfile(data.maps.at(Handedness::Right)).profile;
    CHECK(evr::input::actionPromptText(GameAction::WeaponSlot2, profile, labels) == "A Touch");
    // Built-in data has no touch to name.
    const auto plain = evr::input::buttonLabelsFor(
        evr::input::parseControllerData(evr::game::builtinControllerData(Controller::OculusTouch)));
    CHECK(plain.name(Hand::Right, LabelInput::PrimaryTouch).empty());
}

TEST_CASE("the trigger's touch is an OpenXR action of its own") {
    using evr::input::xrAction;
    CHECK(xrAction(XrActionId::TriggerTouch).name == "trigger_touch");
    CHECK(xrAction(XrActionId::TriggerTouch).kind == evr::input::XrActionKind::Boolean);
    CHECK(evr::input::findXrAction(evr::input::XrActionSetId::Gameplay, "trigger_touch") ==
          XrActionId::TriggerTouch);
    CHECK(evr::input::isValidXrName(xrAction(XrActionId::TriggerTouch).name));
    // A data file may name it in its [profile] section.
    const auto data = evr::input::parseControllerData(R"([profile]
"path" = "/interaction_profiles/oculus/touch_controller"
"gameplay.right.trigger_touch" = "/input/trigger/touch"
)");
    CHECK(data.ok());
    REQUIRE(data.find(XrActionId::TriggerTouch, Hand::Right) != nullptr);
    CHECK(data.find(XrActionId::TriggerTouch, Hand::Right)->path == "/user/hand/right/input/trigger/touch");
}

TEST_CASE("scripted input touches the trigger") {
    const auto input = evr::input::parseTestInput("right.trigger_touch = 1\nleft.trigger_touch = 0\n");
    CHECK(input.issues.empty());
    evr::input::InputFrame frame;
    frame.left.triggerTouch = true;
    evr::input::applyTestInput(input, frame);
    CHECK(frame.right.triggerTouch);
    CHECK_FALSE(frame.left.triggerTouch);
    CHECK(frame.right.trigger == 0.0f); // a touch, not a pull
}
