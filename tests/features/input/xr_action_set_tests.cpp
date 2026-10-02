#include "features/input/xr_action_set.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <ostream>
#include <set>
#include <string>
#include <utility>

using evr::input::findXrAction;
using evr::input::findXrActionSet;
using evr::input::Hand;
using evr::input::handPath;
using evr::input::isValidXrName;
using evr::input::xrAction;
using evr::input::XrActionDef;
using evr::input::XrActionId;
using evr::input::XrActionKind;
using evr::input::xrActions;
using evr::input::xrActionSet;
using evr::input::XrActionSetDef;
using evr::input::XrActionSetId;
using evr::input::xrActionSets;

TEST_CASE("the tables are indexed by their ids") {
    for (std::size_t i = 0; i < xrActions().size(); ++i) {
        CHECK(static_cast<std::size_t>(xrActions()[i].id) == i);
    }
    for (std::size_t i = 0; i < xrActionSets().size(); ++i) {
        CHECK(static_cast<std::size_t>(xrActionSets()[i].id) == i);
    }
    CHECK(xrActionSets().size() == static_cast<std::size_t>(XrActionSetId::Count));
}

TEST_CASE("every name is a valid OpenXR name, unique within its set") {
    std::set<std::pair<XrActionSetId, std::string>> seen;
    for (const XrActionDef& action : xrActions()) {
        CAPTURE(action.name);
        CHECK(isValidXrName(action.name));
        CHECK_FALSE(action.localizedName.empty());
        CHECK(seen.emplace(action.set, std::string(action.name)).second);
    }
    for (const XrActionSetDef& set : xrActionSets()) {
        CHECK(isValidXrName(set.name));
    }
}

TEST_CASE("the menu set outranks gameplay") {
    CHECK(xrActionSet(XrActionSetId::Menu).priority > xrActionSet(XrActionSetId::Gameplay).priority);
}

TEST_CASE("gameplay actions carry every field of the hand state with the right type") {
    CHECK(xrAction(XrActionId::Trigger).kind == XrActionKind::Float);
    CHECK(xrAction(XrActionId::Grip).kind == XrActionKind::Float);
    CHECK(xrAction(XrActionId::Thumbstick).kind == XrActionKind::Vector2);
    CHECK(xrAction(XrActionId::ThumbstickClick).kind == XrActionKind::Boolean);
    CHECK(xrAction(XrActionId::Primary).kind == XrActionKind::Boolean);
    CHECK(xrAction(XrActionId::Secondary).kind == XrActionKind::Boolean);
    CHECK(xrAction(XrActionId::Face3).kind == XrActionKind::Boolean);
    CHECK(xrAction(XrActionId::Face4).kind == XrActionKind::Boolean);
    CHECK(xrAction(XrActionId::Shoulder).kind == XrActionKind::Boolean);
    CHECK(xrAction(XrActionId::Menu).kind == XrActionKind::Boolean);
    CHECK(xrAction(XrActionId::AimPose).kind == XrActionKind::Pose);
    CHECK(xrAction(XrActionId::GripPose).kind == XrActionKind::Pose);
    CHECK(xrAction(XrActionId::Haptic).kind == XrActionKind::Haptic);
    for (const XrActionId id : {XrActionId::Trigger, XrActionId::AimPose, XrActionId::Haptic}) {
        CHECK(xrAction(id).set == XrActionSetId::Gameplay);
    }
}

TEST_CASE("actions are found by set and name") {
    CHECK(findXrActionSet("gameplay") == XrActionSetId::Gameplay);
    CHECK(findXrActionSet("menu") == XrActionSetId::Menu);
    CHECK_FALSE(findXrActionSet("Gameplay").has_value());
    CHECK(findXrAction(XrActionSetId::Gameplay, "trigger") == XrActionId::Trigger);
    CHECK(findXrAction(XrActionSetId::Menu, "select") == XrActionId::MenuSelect);
    CHECK(findXrAction(XrActionSetId::Gameplay, "face3") == XrActionId::Face3);
    CHECK(findXrAction(XrActionSetId::Gameplay, "face4") == XrActionId::Face4);
    CHECK(findXrAction(XrActionSetId::Gameplay, "shoulder") == XrActionId::Shoulder);
    // Names are looked up within their set only.
    CHECK_FALSE(findXrAction(XrActionSetId::Menu, "trigger").has_value());
    CHECK_FALSE(findXrAction(XrActionSetId::Gameplay, "select").has_value());
}

TEST_CASE("subaction paths name the hands") {
    CHECK(handPath(Hand::Left) == "/user/hand/left");
    CHECK(handPath(Hand::Right) == "/user/hand/right");
}

TEST_CASE("OpenXR name rules") {
    CHECK(isValidXrName("thumbstick_click"));
    CHECK(isValidXrName("a-b.c_9"));
    CHECK_FALSE(isValidXrName(""));
    CHECK_FALSE(isValidXrName("Trigger"));
    CHECK_FALSE(isValidXrName("fire weapon"));
    CHECK_FALSE(isValidXrName(std::string(64, 'a')));
    CHECK(isValidXrName(std::string(63, 'a')));
}
