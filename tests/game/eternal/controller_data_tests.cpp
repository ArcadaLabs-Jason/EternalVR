#include "game/eternal/controller_data.hpp"

#include "features/input/binding_compiler.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/interaction_profiles.hpp"
#include "features/input/xr_action_set.hpp"
#include "game/eternal/quest_touch_bindings.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <array>
#include <ostream>
#include <string>

using evr::game::builtinControllerData;
using evr::game::Controller;
using evr::game::controllerName;
using evr::game::Handedness;
using evr::input::BindingIssue;
using evr::input::buildBindingProfile;
using evr::input::ControllerData;
using evr::input::findInteractionProfile;
using evr::input::Hand;
using evr::input::parseControllerData;
using evr::input::xrAction;
using evr::input::XrActionDef;
using evr::input::XrActionId;
using evr::input::xrActions;
using evr::input::XrActionSetId;
using evr::test::questTouchBindings;

namespace {

constexpr std::array kControllers{Controller::OculusTouch, Controller::ValveIndex};
constexpr std::array kAllHandedness{Handedness::Right, Handedness::LeftButtonSwap,
                                    Handedness::LeftButtonAndStickSwap};

std::string describe(const ControllerData& data) {
    std::string text;
    for (const BindingIssue& issue : data.issues) {
        text += "line " + std::to_string(issue.line) + ": " + issue.message + "\n";
    }
    return text;
}

} // namespace

TEST_CASE("the built-in controller data reads without issues") {
    for (const Controller controller : kControllers) {
        CAPTURE(controllerName(controller));
        const ControllerData data = parseControllerData(builtinControllerData(controller));
        INFO(describe(data));
        CHECK(data.ok());
        CHECK(findInteractionProfile(data.profilePath) != nullptr);
    }
    CHECK(parseControllerData(builtinControllerData(Controller::OculusTouch)).profilePath ==
          "/interaction_profiles/oculus/touch_controller");
    CHECK(parseControllerData(builtinControllerData(Controller::ValveIndex)).profilePath ==
          "/interaction_profiles/valve/index_controller");
}

TEST_CASE("every gameplay action is bound on both hands, except where a controller lacks the button") {
    for (const Controller controller : kControllers) {
        CAPTURE(controllerName(controller));
        const ControllerData data = parseControllerData(builtinControllerData(controller));
        for (const XrActionDef& action : xrActions()) {
            if (action.set != XrActionSetId::Gameplay) {
                continue;
            }
            for (const Hand hand : {Hand::Left, Hand::Right}) {
                CAPTURE(action.name);
                CAPTURE(static_cast<int>(hand));
                // Only the left Menu button can be read on either controller family.
                const bool expected = !(action.id == XrActionId::Menu && hand == Hand::Right);
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
        const ControllerData data = parseControllerData(builtinControllerData(controller));
        for (const Handedness handedness : kAllHandedness) {
            CAPTURE(controllerName(controller));
            CAPTURE(static_cast<int>(handedness));
            REQUIRE(data.maps.contains(handedness));
            const auto built = buildBindingProfile(data.maps.at(handedness));
            CHECK(built.ok());
            CHECK(built.profile.weaponHand == (handedness == Handedness::Right ? Hand::Right : Hand::Left));
            CHECK(built.profile.moveStick.has_value());
            CHECK(built.profile.turnStick.has_value());
        }
    }
}

TEST_CASE("the Touch data file's maps are the built-in Quest Touch maps") {
    const ControllerData data = parseControllerData(builtinControllerData(Controller::OculusTouch));
    for (const Handedness handedness : kAllHandedness) {
        CAPTURE(static_cast<int>(handedness));
        CHECK(data.maps.at(handedness) == questTouchBindings(handedness));
    }
}

TEST_CASE("Index keeps the Touch layout, with its own inputs underneath") {
    const ControllerData touch = parseControllerData(builtinControllerData(Controller::OculusTouch));
    const ControllerData index = parseControllerData(builtinControllerData(Controller::ValveIndex));
    CHECK(index.maps == touch.maps);
    // Left A/B take the X/Y roles; grip reads force; the menu input is a trackpad press.
    CHECK(index.find(XrActionId::Primary, Hand::Left)->path == "/user/hand/left/input/a/click");
    CHECK(index.find(XrActionId::Grip, Hand::Right)->path == "/user/hand/right/input/squeeze/force");
    CHECK(index.find(XrActionId::Menu, Hand::Left)->path == "/user/hand/left/input/trackpad/force");
    CHECK(touch.find(XrActionId::Primary, Hand::Left)->path == "/user/hand/left/input/x/click");
}

TEST_CASE("controller names match the data files") {
    CHECK(controllerName(Controller::OculusTouch) == "oculus_touch");
    CHECK(controllerName(Controller::ValveIndex) == "valve_index");
    CHECK(xrAction(XrActionId::GripPose).name == "grip_pose");
}
