#include "game/eternal/controller_data.hpp"

#include "features/input/binding_compiler.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/interaction_profiles.hpp"
#include "features/input/xr_action_set.hpp"
#include "game/eternal/quest_touch_bindings.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>

using evr::game::builtinControllerData;
using evr::game::Controller;
using evr::game::controllerName;
using evr::game::GameAction;
using evr::game::gameActionName;
using evr::game::Handedness;
using evr::game::kControllers;
using evr::input::BindingIssue;
using evr::input::BindingProfile;
using evr::input::buildBindingProfile;
using evr::input::ButtonBinding;
using evr::input::ButtonInput;
using evr::input::ControllerData;
using evr::input::findInteractionProfile;
using evr::input::Hand;
using evr::input::InteractionProfileInfo;
using evr::input::knownInteractionProfiles;
using evr::input::parseControllerData;
using evr::input::PressKind;
using evr::input::profileAvailable;
using evr::input::xrAction;
using evr::input::XrActionDef;
using evr::input::XrActionId;
using evr::input::xrActions;
using evr::input::XrActionSetId;
using evr::test::questTouchBindings;

namespace {

constexpr std::array kAllHandedness{Handedness::Right, Handedness::LeftButtonSwap,
                                    Handedness::LeftButtonAndStickSwap};

// The families whose controllers have Touch's buttons in Touch's places, and so Touch's maps.
constexpr std::array kTouchLayout{Controller::OculusTouch, Controller::ValveIndex, Controller::HpReverbG2,
                                  Controller::ViveCosmos, Controller::Pico4};

std::string describe(const ControllerData& data) {
    std::string text;
    for (const BindingIssue& issue : data.issues) {
        text += "line " + std::to_string(issue.line) + ": " + issue.message + "\n";
    }
    return text;
}

ControllerData dataOf(Controller controller) {
    return parseControllerData(builtinControllerData(controller));
}

BindingProfile mapOf(Controller controller, Handedness handedness) {
    return buildBindingProfile(dataOf(controller).maps.at(handedness)).profile;
}

std::string_view expectedProfile(Controller controller) {
    switch (controller) {
    case Controller::OculusTouch:
        return "/interaction_profiles/oculus/touch_controller";
    case Controller::ValveIndex:
        return "/interaction_profiles/valve/index_controller";
    case Controller::HpReverbG2:
        return "/interaction_profiles/hp/mixed_reality_controller";
    case Controller::WindowsMixedReality:
        return "/interaction_profiles/microsoft/motion_controller";
    case Controller::ViveCosmos:
        return "/interaction_profiles/htc/vive_cosmos_controller";
    case Controller::ViveWand:
        return "/interaction_profiles/htc/vive_controller";
    case Controller::Pico4:
        return "/interaction_profiles/bytedance/pico4_controller";
    case Controller::Count:
        break;
    }
    return {};
}

// The gameplay actions a family leaves unbound on a hand, because the controller has no input for them.
bool expectedUnbound(Controller controller, XrActionId action, Hand hand) {
    // Only the left Menu button is the pause on every family (the capture chord reads it too).
    if (action == XrActionId::Menu && hand == Hand::Right) {
        return true;
    }
    switch (controller) {
    case Controller::WindowsMixedReality:
        // No face buttons: the trackpad clicks are the primary buttons, the right Menu button the right
        // secondary one, and the left Menu button the pause.
        return action == XrActionId::Secondary && hand == Hand::Left;
    case Controller::ViveWand:
        // A trigger, a grip, a trackpad and a Menu button: the right Menu button is the right secondary.
        return action == XrActionId::Primary || (action == XrActionId::Secondary && hand == Hand::Left);
    default:
        return false;
    }
}

bool reaches(const BindingProfile& profile, GameAction action) {
    return std::ranges::any_of(profile.buttons,
                               [action](const ButtonBinding& b) { return b.action == action; }) ||
           std::ranges::any_of(profile.stickGestures, [action](const auto& g) { return g.action == action; });
}

const ButtonBinding* bindingOf(const BindingProfile& profile, GameAction action) {
    const auto it = std::ranges::find_if(profile.buttons,
                                         [action](const ButtonBinding& b) { return b.action == action; });
    return it == profile.buttons.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("the built-in controller data reads without issues") {
    for (const Controller controller : kControllers) {
        CAPTURE(controllerName(controller));
        const ControllerData data = dataOf(controller);
        INFO(describe(data));
        CHECK(data.ok());
        CHECK(data.profilePath == expectedProfile(controller));
        CHECK(findInteractionProfile(data.profilePath) != nullptr);
    }
}

TEST_CASE("every family has a profile of its own, and every known profile has a family") {
    std::set<std::string> paths;
    std::set<std::string_view> names;
    for (const Controller controller : kControllers) {
        CHECK(paths.insert(dataOf(controller).profilePath).second);
        CHECK(names.insert(controllerName(controller)).second);
    }
    for (const InteractionProfileInfo& profile : knownInteractionProfiles()) {
        CAPTURE(profile.path);
        CHECK(paths.contains(std::string(profile.path)));
    }
    CHECK(paths.size() == knownInteractionProfiles().size());
}

TEST_CASE("every gameplay action is bound on both hands, except where a controller lacks the button") {
    for (const Controller controller : kControllers) {
        CAPTURE(controllerName(controller));
        const ControllerData data = dataOf(controller);
        for (const XrActionDef& action : xrActions()) {
            if (action.set != XrActionSetId::Gameplay) {
                continue;
            }
            for (const Hand hand : {Hand::Left, Hand::Right}) {
                CAPTURE(action.name);
                CAPTURE(static_cast<int>(hand));
                const bool expected = !expectedUnbound(controller, action.id, hand);
                CHECK((data.find(action.id, hand) != nullptr) == expected);
            }
        }
        // Menus can be pointed at and confirmed with either hand.
        for (const Hand hand : {Hand::Left, Hand::Right}) {
            CHECK(data.find(XrActionId::MenuPointerPose, hand) != nullptr);
            CHECK(data.find(XrActionId::MenuSelect, hand) != nullptr);
        }
    }
}

TEST_CASE("every default control map compiles without issues") {
    for (const Controller controller : kControllers) {
        const ControllerData data = dataOf(controller);
        for (const Handedness handedness : kAllHandedness) {
            CAPTURE(controllerName(controller));
            CAPTURE(static_cast<int>(handedness));
            REQUIRE(data.maps.contains(handedness));
            const auto built = buildBindingProfile(data.maps.at(handedness));
            std::string issues;
            for (const BindingIssue& issue : built.issues) {
                issues += issue.message + "\n";
            }
            INFO(issues);
            CHECK(built.ok());
            CHECK(built.profile.weaponHand == (handedness == Handedness::Right ? Hand::Right : Hand::Left));
            CHECK(built.profile.moveStick.has_value());
            CHECK(built.profile.turnStick.has_value());
        }
    }
}

TEST_CASE("every map binds only inputs the family's profile binds") {
    for (const Controller controller : kControllers) {
        const ControllerData data = dataOf(controller);
        for (const Handedness handedness : kAllHandedness) {
            CAPTURE(controllerName(controller));
            CAPTURE(static_cast<int>(handedness));
            const BindingProfile profile = mapOf(controller, handedness);
            for (const ButtonBinding& b : profile.buttons) {
                CAPTURE(gameActionName(b.action));
                const XrActionId action = b.input == ButtonInput::Trigger      ? XrActionId::Trigger
                                          : b.input == ButtonInput::Grip       ? XrActionId::Grip
                                          : b.input == ButtonInput::StickClick ? XrActionId::ThumbstickClick
                                          : b.input == ButtonInput::Primary    ? XrActionId::Primary
                                          : b.input == ButtonInput::Secondary  ? XrActionId::Secondary
                                                                               : XrActionId::Menu;
                CHECK(data.find(action, b.hand) != nullptr);
            }
        }
    }
}

TEST_CASE("the pause is a tap on the left Menu button in every map") {
    // The capture chord holds the left Menu button, so it must never carry a gameplay action.
    for (const Controller controller : kControllers) {
        for (const Handedness handedness : kAllHandedness) {
            CAPTURE(controllerName(controller));
            CAPTURE(static_cast<int>(handedness));
            const BindingProfile profile = mapOf(controller, handedness);
            const ButtonBinding* pause = bindingOf(profile, GameAction::Pause);
            REQUIRE(pause != nullptr);
            CHECK(pause->hand == Hand::Left);
            CHECK(pause->input == ButtonInput::Menu);
            CHECK(pause->kind == PressKind::Tap);
        }
    }
}

TEST_CASE("the essential actions are reachable on every family and handedness") {
    for (const Controller controller : kControllers) {
        for (const Handedness handedness : kAllHandedness) {
            CAPTURE(controllerName(controller));
            CAPTURE(static_cast<int>(handedness));
            const BindingProfile profile = mapOf(controller, handedness);
            for (const GameAction action :
                 {GameAction::Fire, GameAction::WeaponMod, GameAction::Jump, GameAction::Dash,
                  GameAction::Melee, GameAction::Chainsaw, GameAction::FlameBelch, GameAction::Equipment,
                  GameAction::SwitchEquipment, GameAction::QuickSwitch, GameAction::WeaponWheel,
                  GameAction::Pause, GameAction::Dossier}) {
                CAPTURE(gameActionName(action));
                CHECK(reaches(profile, action));
            }
            // Fire, jump, dash and melee go out the moment the input goes down.
            for (const GameAction action :
                 {GameAction::Fire, GameAction::Jump, GameAction::Dash, GameAction::Melee}) {
                CAPTURE(gameActionName(action));
                const ButtonBinding* binding = bindingOf(profile, action);
                REQUIRE(binding != nullptr);
                CHECK(binding->kind == PressKind::WhileDown);
            }
            const ButtonBinding* fire = bindingOf(profile, GameAction::Fire);
            CHECK(fire->input == ButtonInput::Trigger);
            CHECK(fire->hand == profile.weaponHand);
        }
    }
}

TEST_CASE("the Touch data file's maps are the built-in Quest Touch maps") {
    const ControllerData data = dataOf(Controller::OculusTouch);
    for (const Handedness handedness : kAllHandedness) {
        CAPTURE(static_cast<int>(handedness));
        CHECK(data.maps.at(handedness) == questTouchBindings(handedness));
    }
}

TEST_CASE("families with Touch's buttons keep the Touch maps") {
    const ControllerData touch = dataOf(Controller::OculusTouch);
    for (const Controller controller : kTouchLayout) {
        CAPTURE(controllerName(controller));
        CHECK(dataOf(controller).maps == touch.maps);
    }
}

TEST_CASE("Index keeps the Touch layout, with its own inputs underneath") {
    const ControllerData touch = dataOf(Controller::OculusTouch);
    const ControllerData index = dataOf(Controller::ValveIndex);
    // Left A/B take the X/Y roles; grip reads force; the menu input is a trackpad press.
    CHECK(index.find(XrActionId::Primary, Hand::Left)->path == "/user/hand/left/input/a/click");
    CHECK(index.find(XrActionId::Grip, Hand::Right)->path == "/user/hand/right/input/squeeze/force");
    CHECK(index.find(XrActionId::Menu, Hand::Left)->path == "/user/hand/left/input/trackpad/force");
    CHECK(touch.find(XrActionId::Primary, Hand::Left)->path == "/user/hand/left/input/x/click");
}

TEST_CASE("G2 and Cosmos read their own buttons") {
    const ControllerData g2 = dataOf(Controller::HpReverbG2);
    CHECK(g2.find(XrActionId::Primary, Hand::Left)->path == "/user/hand/left/input/x/click");
    CHECK(g2.find(XrActionId::Secondary, Hand::Right)->path == "/user/hand/right/input/b/click");
    CHECK(g2.find(XrActionId::Grip, Hand::Right)->path == "/user/hand/right/input/squeeze/value");
    CHECK(g2.find(XrActionId::Menu, Hand::Left)->path == "/user/hand/left/input/menu/click");
    const ControllerData cosmos = dataOf(Controller::ViveCosmos);
    // The Cosmos grip is a button.
    CHECK(cosmos.find(XrActionId::Grip, Hand::Left)->path == "/user/hand/left/input/squeeze/click");
    CHECK(cosmos.find(XrActionId::Primary, Hand::Right)->path == "/user/hand/right/input/a/click");
}

TEST_CASE("Windows Mixed Reality: trackpad clicks and the right Menu button stand in for the face buttons") {
    const ControllerData data = dataOf(Controller::WindowsMixedReality);
    CHECK(data.find(XrActionId::Thumbstick, Hand::Left)->path == "/user/hand/left/input/thumbstick");
    CHECK(data.find(XrActionId::Primary, Hand::Left)->path == "/user/hand/left/input/trackpad/click");
    CHECK(data.find(XrActionId::Primary, Hand::Right)->path == "/user/hand/right/input/trackpad/click");
    CHECK(data.find(XrActionId::Secondary, Hand::Right)->path == "/user/hand/right/input/menu/click");
    CHECK(data.find(XrActionId::Menu, Hand::Left)->path == "/user/hand/left/input/menu/click");
    const BindingProfile right = mapOf(Controller::WindowsMixedReality, Handedness::Right);
    CHECK(bindingOf(right, GameAction::Jump)->input == ButtonInput::Primary);
    CHECK(bindingOf(right, GameAction::Dash)->input == ButtonInput::Secondary);
    CHECK(bindingOf(right, GameAction::SwitchWeaponMod)->input == ButtonInput::StickClick);
    CHECK(bindingOf(right, GameAction::Crucible)->kind == PressKind::Hold);
    // The full mirror keeps the dash on the right Menu button: the left one is the pause.
    const BindingProfile mirror = mapOf(Controller::WindowsMixedReality, Handedness::LeftButtonAndStickSwap);
    CHECK(bindingOf(mirror, GameAction::Dash)->hand == Hand::Right);
    CHECK(bindingOf(mirror, GameAction::Jump)->hand == Hand::Left);
}

TEST_CASE("Vive wands: the trackpad is the stick, and the off hand's trigger and grip carry tap and hold") {
    const ControllerData data = dataOf(Controller::ViveWand);
    CHECK(data.find(XrActionId::Thumbstick, Hand::Right)->path == "/user/hand/right/input/trackpad");
    CHECK(data.find(XrActionId::ThumbstickClick, Hand::Left)->path == "/user/hand/left/input/trackpad/click");
    CHECK(data.find(XrActionId::Grip, Hand::Left)->path == "/user/hand/left/input/squeeze/click");
    CHECK(data.find(XrActionId::Secondary, Hand::Right)->path == "/user/hand/right/input/menu/click");
    for (const Handedness handedness : kAllHandedness) {
        CAPTURE(static_cast<int>(handedness));
        const BindingProfile profile = mapOf(Controller::ViveWand, handedness);
        const Hand offHand = profile.weaponHand == Hand::Right ? Hand::Left : Hand::Right;
        const ButtonBinding* dossier = bindingOf(profile, GameAction::Dossier);
        CHECK(dossier->input == ButtonInput::Grip);
        CHECK(dossier->kind == PressKind::Hold);
        CHECK(dossier->hand == offHand);
        CHECK(bindingOf(profile, GameAction::SwitchEquipment)->kind == PressKind::Tap);
        CHECK(bindingOf(profile, GameAction::Equipment)->kind == PressKind::Tap);
        CHECK(bindingOf(profile, GameAction::FlameBelch)->kind == PressKind::Hold);
        CHECK(bindingOf(profile, GameAction::Dash)->hand == Hand::Right);
    }
}

TEST_CASE("extension profiles are available only with their extension, or as OpenXR 1.1 core") {
    const InteractionProfileInfo& touch =
        *findInteractionProfile("/interaction_profiles/oculus/touch_controller");
    const InteractionProfileInfo& wmr =
        *findInteractionProfile("/interaction_profiles/microsoft/motion_controller");
    const InteractionProfileInfo& vive = *findInteractionProfile("/interaction_profiles/htc/vive_controller");
    const InteractionProfileInfo& g2 =
        *findInteractionProfile("/interaction_profiles/hp/mixed_reality_controller");
    const InteractionProfileInfo& cosmos =
        *findInteractionProfile("/interaction_profiles/htc/vive_cosmos_controller");
    const InteractionProfileInfo& pico =
        *findInteractionProfile("/interaction_profiles/bytedance/pico4_controller");
    CHECK(g2.extension == "XR_EXT_hp_mixed_reality_controller");
    CHECK(cosmos.extension == "XR_HTC_vive_cosmos_controller_interaction");
    CHECK(pico.extension == "XR_BD_controller_interaction");
    constexpr std::array<std::string_view, 0> kNone{};
    constexpr std::array<std::string_view, 1> kHp{"XR_EXT_hp_mixed_reality_controller"};
    for (const InteractionProfileInfo* core : {&touch, &wmr, &vive}) {
        CHECK(core->extension.empty());
        CHECK(profileAvailable(*core, kNone, false));
    }
    CHECK_FALSE(profileAvailable(g2, kNone, false));
    CHECK(profileAvailable(g2, kHp, false));
    CHECK(profileAvailable(g2, kNone, true));
    CHECK_FALSE(profileAvailable(cosmos, kHp, false));
    CHECK(profileAvailable(cosmos, kNone, true));
}

TEST_CASE("controller names match the data files") {
    CHECK(controllerName(Controller::OculusTouch) == "oculus_touch");
    CHECK(controllerName(Controller::ValveIndex) == "valve_index");
    CHECK(controllerName(Controller::HpReverbG2) == "hp_reverb_g2");
    CHECK(controllerName(Controller::WindowsMixedReality) == "windows_mixed_reality");
    CHECK(controllerName(Controller::ViveCosmos) == "htc_vive_cosmos");
    CHECK(controllerName(Controller::ViveWand) == "htc_vive_wand");
    CHECK(controllerName(Controller::Pico4) == "pico4");
    for (std::size_t i = 0; i < kControllers.size(); ++i) {
        CHECK(static_cast<std::size_t>(kControllers[i]) == i);
    }
    CHECK(xrAction(XrActionId::GripPose).name == "grip_pose");
}
