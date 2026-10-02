#include "features/input/button_labels.hpp"

#include "features/input/binding_compiler.hpp"
#include "features/input/controller_bindings.hpp"
#include "game/eternal/controller_data.hpp"

#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <vector>

using evr::game::builtinControllerData;
using evr::game::Controller;
using evr::game::controllerName;
using evr::game::GameAction;
using evr::game::Handedness;
using evr::game::kControllers;
using evr::input::actionPromptText;
using evr::input::BindingIssueKind;
using evr::input::BindingProfile;
using evr::input::buildBindingProfile;
using evr::input::ButtonBinding;
using evr::input::ButtonInput;
using evr::input::ButtonLabels;
using evr::input::buttonLabelsFor;
using evr::input::ControllerData;
using evr::input::formatLabelKey;
using evr::input::Hand;
using evr::input::LabelInput;
using evr::input::LabelKey;
using evr::input::nameFromInputPath;
using evr::input::parseControllerData;
using evr::input::parseLabelKey;
using evr::input::PressKind;
using evr::input::PromptControl;
using evr::input::promptControls;
using evr::input::PromptLabelSet;
using evr::input::promptLabelSet;
using evr::input::PromptPress;
using evr::input::promptText;
using evr::input::StickGesture;

namespace {

ControllerData builtin(Controller controller) {
    return parseControllerData(builtinControllerData(controller));
}

BindingProfile builtinProfile(Controller controller, Handedness handedness) {
    return buildBindingProfile(builtin(controller).maps.at(handedness)).profile;
}

std::string prompt(Controller controller, GameAction action, Handedness handedness = Handedness::Right) {
    const ControllerData data = builtin(controller);
    return actionPromptText(action, buildBindingProfile(data.maps.at(handedness)).profile,
                            buttonLabelsFor(data));
}

std::string touchPrompt(GameAction action, Handedness handedness = Handedness::Right) {
    return prompt(Controller::OculusTouch, action, handedness);
}

const std::string kTouchHeader = "[profile]\n\"path\" = \"/interaction_profiles/oculus/touch_controller\"\n"
                                 "\"gameplay.left.primary\" = \"/input/x/click\"\n"
                                 "\"gameplay.right.primary\" = \"/input/a/click\"\n";

} // namespace

TEST_CASE("label keys are <hand>.<input>") {
    CHECK(parseLabelKey("left.primary") == LabelKey{Hand::Left, LabelInput::Primary});
    CHECK(parseLabelKey("right.stick") == LabelKey{Hand::Right, LabelInput::Stick});
    CHECK(parseLabelKey("right.stick_click") == LabelKey{Hand::Right, LabelInput::StickClick});
    CHECK(parseLabelKey("left.face4") == LabelKey{Hand::Left, LabelInput::Face4});
    CHECK(parseLabelKey("right.shoulder") == LabelKey{Hand::Right, LabelInput::Shoulder});
    CHECK_FALSE(parseLabelKey("left"));
    CHECK_FALSE(parseLabelKey("middle.primary"));
    CHECK_FALSE(parseLabelKey("left.primary.press"));
    CHECK(formatLabelKey({Hand::Left, LabelInput::Menu}) == "left.menu");
}

TEST_CASE("an input path names its control") {
    CHECK(nameFromInputPath("/user/hand/left/input/x/click") == "X");
    CHECK(nameFromInputPath("/input/a/click") == "A");
    CHECK(nameFromInputPath("/input/trigger/value") == "Trigger");
    CHECK(nameFromInputPath("/input/squeeze/value") == "Grip");
    CHECK(nameFromInputPath("/input/squeeze/force") == "Grip");
    CHECK(nameFromInputPath("/input/thumbstick") == "Stick");
    CHECK(nameFromInputPath("/input/thumbstick/click") == "Stick Click");
    CHECK(nameFromInputPath("/input/trackpad") == "Trackpad");
    CHECK(nameFromInputPath("/input/trackpad/click") == "Trackpad Click");
    CHECK(nameFromInputPath("/input/trackpad/force") == "Trackpad Press");
    CHECK(nameFromInputPath("/input/menu/click") == "Menu");
    CHECK(nameFromInputPath("/input/dpad_up/click") == "D-pad Up");
    CHECK(nameFromInputPath("/input/shoulder/click") == "Bumper");
    CHECK(nameFromInputPath("/input/bumper/click") == "Bumper");
    CHECK(nameFromInputPath("/input/view/click") == "View");
    CHECK(nameFromInputPath("/input/some_new_button/click") == "Some New Button");
    CHECK(nameFromInputPath("/output/haptic").empty());
}

TEST_CASE("Touch controllers: the hand is named only where both controllers have the control") {
    const ButtonLabels labels = buttonLabelsFor(builtin(Controller::OculusTouch));
    CHECK(labels.name(Hand::Left, LabelInput::Primary) == "X");
    CHECK(labels.name(Hand::Left, LabelInput::Secondary) == "Y");
    CHECK(labels.name(Hand::Right, LabelInput::Primary) == "A");
    CHECK(labels.name(Hand::Right, LabelInput::Secondary) == "B");
    CHECK(labels.name(Hand::Right, LabelInput::Trigger) == "Right Trigger");
    CHECK(labels.name(Hand::Left, LabelInput::Grip) == "Left Grip");
    CHECK(labels.name(Hand::Right, LabelInput::StickClick) == "Right Stick Click");
    CHECK(labels.name(Hand::Right, LabelInput::Stick) == "Right Stick");
    CHECK(labels.name(Hand::Left, LabelInput::Menu) == "Menu");
    // Only the left Menu button can be read on Touch controllers.
    CHECK(labels.name(Hand::Right, LabelInput::Menu).empty());
}

TEST_CASE("Index controllers: an A on both hands is named with its hand") {
    const ButtonLabels labels = buttonLabelsFor(builtin(Controller::ValveIndex));
    CHECK(labels.name(Hand::Left, LabelInput::Primary) == "Left A");
    CHECK(labels.name(Hand::Right, LabelInput::Primary) == "Right A");
    CHECK(labels.name(Hand::Right, LabelInput::Secondary) == "Right B");
    CHECK(labels.name(Hand::Left, LabelInput::Menu) == "Left Trackpad Press");
}

TEST_CASE("Vive wands: the trackpad stands in for the stick") {
    const ButtonLabels labels = buttonLabelsFor(builtin(Controller::ViveWand));
    CHECK(labels.name(Hand::Right, LabelInput::Stick) == "Right Trackpad");
    CHECK(labels.name(Hand::Right, LabelInput::StickClick) == "Right Trackpad Click");
    CHECK(labels.name(Hand::Right, LabelInput::Secondary) == "Right Menu");
    CHECK(labels.name(Hand::Left, LabelInput::Primary).empty());
}

TEST_CASE("Steam Frame controllers: face buttons and the D-pad on one hand, bumpers on both") {
    const ButtonLabels labels = buttonLabelsFor(builtin(Controller::SteamFrame));
    CHECK(labels.name(Hand::Right, LabelInput::Primary) == "A");
    CHECK(labels.name(Hand::Right, LabelInput::Face3) == "X");
    CHECK(labels.name(Hand::Right, LabelInput::Face4) == "Y");
    CHECK(labels.name(Hand::Left, LabelInput::Primary) == "D-pad Down");
    CHECK(labels.name(Hand::Left, LabelInput::Secondary) == "D-pad Left");
    CHECK(labels.name(Hand::Left, LabelInput::Face4) == "D-pad Up");
    CHECK(labels.name(Hand::Left, LabelInput::Menu) == "View");
    CHECK(labels.name(Hand::Right, LabelInput::Menu) == "Menu");
    CHECK(labels.name(Hand::Right, LabelInput::Shoulder) == "Right Bumper");
    CHECK(labels.name(Hand::Left, LabelInput::Shoulder) == "Left Bumper");
    CHECK(labels.name(Hand::Right, LabelInput::Trigger) == "Right Trigger");
    CHECK(labels.name(Hand::Left, LabelInput::Stick) == "Left Stick");
}

TEST_CASE("prompt text for the Steam Frame's default right-handed map") {
    CHECK(prompt(Controller::SteamFrame, GameAction::Fire) == "Right Trigger");
    CHECK(prompt(Controller::SteamFrame, GameAction::Jump) == "A");
    CHECK(prompt(Controller::SteamFrame, GameAction::Dash) == "B");
    CHECK(prompt(Controller::SteamFrame, GameAction::Chainsaw) == "X");
    CHECK(prompt(Controller::SteamFrame, GameAction::FlameBelch) == "Left Grip");
    CHECK(prompt(Controller::SteamFrame, GameAction::Equipment) == "Left Bumper");
    CHECK(prompt(Controller::SteamFrame, GameAction::SwitchWeaponMod) == "D-pad Up");
    CHECK(prompt(Controller::SteamFrame, GameAction::SwitchEquipment) == "D-pad Left");
    CHECK(prompt(Controller::SteamFrame, GameAction::MissionInfo) == "D-pad Down");
    CHECK(prompt(Controller::SteamFrame, GameAction::Pause) == "View");
    CHECK(prompt(Controller::SteamFrame, GameAction::Dossier) == "Menu");
    CHECK(prompt(Controller::SteamFrame, GameAction::QuickSwitch) == "Right Bumper");
    CHECK(prompt(Controller::SteamFrame, GameAction::WeaponWheel) == "Hold Right Bumper");
    CHECK(prompt(Controller::SteamFrame, GameAction::Jump, Handedness::LeftButtonAndStickSwap) ==
          "D-pad Down");
}

TEST_CASE("every built-in family names every input its default maps bind") {
    for (const Controller controller : kControllers) {
        const ControllerData data = builtin(controller);
        const ButtonLabels labels = buttonLabelsFor(data);
        for (const auto& [handedness, entries] : data.maps) {
            const BindingProfile profile = buildBindingProfile(entries).profile;
            for (const ButtonBinding& binding : profile.buttons) {
                for (const PromptControl& control : promptControls(binding.action, profile)) {
                    INFO(controllerName(controller), " ", formatLabelKey({control.hand, control.input}));
                    CHECK_FALSE(promptText(control, labels).empty());
                }
            }
        }
    }
}

TEST_CASE("prompt text for Touch's default right-handed map") {
    CHECK(touchPrompt(GameAction::Fire) == "Right Trigger");
    CHECK(touchPrompt(GameAction::WeaponMod) == "Right Grip");
    CHECK(touchPrompt(GameAction::Jump) == "A");
    CHECK(touchPrompt(GameAction::Dash) == "B");
    CHECK(touchPrompt(GameAction::Melee) == "Right Stick Click");
    CHECK(touchPrompt(GameAction::Equipment) == "Left Trigger");
    CHECK(touchPrompt(GameAction::FlameBelch) == "Left Grip");
    CHECK(touchPrompt(GameAction::SwitchEquipment) == "X");
    CHECK(touchPrompt(GameAction::Dossier) == "Hold X");
    CHECK(touchPrompt(GameAction::SwitchWeaponMod) == "Y");
    CHECK(touchPrompt(GameAction::MissionInfo) == "Hold Y");
    CHECK(touchPrompt(GameAction::Pause) == "Menu");
    CHECK(touchPrompt(GameAction::Chainsaw) == "Right Stick Up");
    CHECK(touchPrompt(GameAction::QuickSwitch) == "Right Stick Down");
    CHECK(touchPrompt(GameAction::WeaponWheel) == "Hold Right Stick Down");
    CHECK(touchPrompt(GameAction::WeaponSlot1).empty());
}

TEST_CASE("prompt text follows the player's handedness") {
    CHECK(touchPrompt(GameAction::Fire, Handedness::LeftButtonSwap) == "Left Trigger");
    CHECK(touchPrompt(GameAction::Jump, Handedness::LeftButtonSwap) == "A");
    CHECK(touchPrompt(GameAction::Jump, Handedness::LeftButtonAndStickSwap) == "X");
    CHECK(touchPrompt(GameAction::Chainsaw, Handedness::LeftButtonAndStickSwap) == "Left Stick Up");
}

TEST_CASE("presses come before holds and holds before stick gestures") {
    BindingProfile profile;
    profile.turnStick = Hand::Right;
    profile.stickGestures.push_back({StickGesture::Up, GameAction::Jump});
    profile.buttons.push_back({Hand::Left, ButtonInput::Primary, PressKind::Hold, GameAction::Jump});
    profile.buttons.push_back({Hand::Right, ButtonInput::Trigger, PressKind::Tap, GameAction::Jump});
    const std::vector<PromptControl> controls = promptControls(GameAction::Jump, profile);
    REQUIRE(controls.size() == 3);
    CHECK(controls[0] == PromptControl{Hand::Right, LabelInput::Trigger, PromptPress::Press});
    CHECK(controls[1] == PromptControl{Hand::Left, LabelInput::Primary, PromptPress::Hold});
    CHECK(controls[2] == PromptControl{Hand::Right, LabelInput::Stick, PromptPress::StickUp});
}

TEST_CASE("an unnamed control is skipped for the next one") {
    BindingProfile profile;
    profile.buttons.push_back({Hand::Right, ButtonInput::Menu, PressKind::WhileDown, GameAction::Pause});
    profile.buttons.push_back({Hand::Left, ButtonInput::Menu, PressKind::WhileDown, GameAction::Pause});
    CHECK(actionPromptText(GameAction::Pause, profile, buttonLabelsFor(builtin(Controller::OculusTouch))) ==
          "Menu");
}

TEST_CASE("[labels] names inputs as written") {
    const ControllerData data =
        parseControllerData(kTouchHeader + "[labels]\n"
                                           "\"left.primary\" = \"Left X\"\n"
                                           "\"right.stick\" = \"Right Thumbstick\"\n");
    CHECK(data.ok());
    const ButtonLabels labels = buttonLabelsFor(data);
    CHECK(labels.name(Hand::Left, LabelInput::Primary) == "Left X");
    CHECK(labels.name(Hand::Right, LabelInput::Stick) == "Right Thumbstick");
    CHECK(labels.name(Hand::Right, LabelInput::Primary) == "A");
}

TEST_CASE("[labels] reports unknown keys and empty names and leaves them out") {
    const ControllerData data = parseControllerData(kTouchHeader + "[labels]\n"
                                                                   "\"left.thumb\" = \"Thumb\"\n"
                                                                   "\"right.primary\" = \"  \"\n");
    REQUIRE(data.issues.size() == 2);
    CHECK(data.issues[0].kind == BindingIssueKind::UnknownKey);
    CHECK(data.issues[0].key == "left.thumb");
    CHECK(data.issues[0].line == 6);
    CHECK(data.issues[1].kind == BindingIssueKind::UnknownValue);
    CHECK(data.issues[1].key == "right.primary");
    CHECK(data.labels.empty());
    CHECK(buttonLabelsFor(data).name(Hand::Right, LabelInput::Primary) == "A");
}

TEST_CASE("for a profile we have no input list for, a name shared by both hands gets its hand") {
    const ControllerData data =
        parseControllerData("[profile]\n\"path\" = \"/interaction_profiles/example/new_controller\"\n"
                            "\"gameplay.left.primary\" = \"/input/dpad_up/click\"\n"
                            "\"gameplay.right.primary\" = \"/input/y/click\"\n"
                            "\"gameplay.left.trigger\" = \"/input/trigger/value\"\n"
                            "\"gameplay.right.trigger\" = \"/input/trigger/value\"\n");
    const ButtonLabels labels = buttonLabelsFor(data);
    CHECK(labels.name(Hand::Left, LabelInput::Primary) == "D-pad Up");
    CHECK(labels.name(Hand::Right, LabelInput::Primary) == "Y");
    CHECK(labels.name(Hand::Left, LabelInput::Trigger) == "Left Trigger");
    CHECK(labels.name(Hand::Right, LabelInput::Trigger) == "Right Trigger");
}

TEST_CASE("the label set gives each distinct text one slot") {
    const ControllerData data = builtin(Controller::OculusTouch);
    const PromptLabelSet set =
        promptLabelSet(buildBindingProfile(data.maps.at(Handedness::Right)).profile, buttonLabelsFor(data));
    const auto at = [&set](GameAction action) {
        return static_cast<std::size_t>(action);
    };
    CHECK(set.text[at(GameAction::Fire)] == "Right Trigger");
    CHECK(set.text[at(GameAction::Dossier)] == "Hold X");
    CHECK(set.text[at(GameAction::WeaponSlot1)].empty());
    CHECK(set.slot[at(GameAction::WeaponSlot1)] == -1);
    for (std::size_t i = 0; i < set.text.size(); ++i) {
        if (!set.text[i].empty()) {
            REQUIRE(set.slot[i] >= 0);
            CHECK(set.distinct[static_cast<std::size_t>(set.slot[i])] == set.text[i]);
        }
    }
}

TEST_CASE("two actions on one button share a slot") {
    BindingProfile profile;
    profile.buttons.push_back({Hand::Right, ButtonInput::Primary, PressKind::WhileDown, GameAction::Jump});
    profile.buttons.push_back({Hand::Right, ButtonInput::Primary, PressKind::WhileDown, GameAction::Dash});
    profile.buttons.push_back({Hand::Right, ButtonInput::Trigger, PressKind::WhileDown, GameAction::Fire});
    const PromptLabelSet set = promptLabelSet(profile, buttonLabelsFor(builtin(Controller::OculusTouch)));
    const auto at = [&set](GameAction action) {
        return static_cast<std::size_t>(action);
    };
    CHECK(set.distinct.size() == 2);
    CHECK(set.slot[at(GameAction::Jump)] == set.slot[at(GameAction::Dash)]);
    CHECK(set.slot[at(GameAction::Fire)] != set.slot[at(GameAction::Jump)]);
}

TEST_CASE("label set texts are printable ASCII") {
    const ControllerData data = parseControllerData(kTouchHeader + "[labels]\n\"right.primary\" = \"\xC3\xA9"
                                                                   "A\"\n");
    BindingProfile profile;
    profile.buttons.push_back({Hand::Right, ButtonInput::Primary, PressKind::WhileDown, GameAction::Jump});
    CHECK(promptLabelSet(profile, buttonLabelsFor(data)).text[static_cast<std::size_t>(GameAction::Jump)] ==
          "??A");
}
