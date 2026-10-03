#include "features/input/controller_bindings.hpp"

#include "features/input/interaction_profiles.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>
#include <string>
#include <string_view>

using evr::game::Handedness;
using evr::input::BindingConflict;
using evr::input::BindingIssue;
using evr::input::BindingIssueKind;
using evr::input::bindingsOverlap;
using evr::input::ControllerData;
using evr::input::findInteractionProfile;
using evr::input::Hand;
using evr::input::handednessName;
using evr::input::InteractionProfileInfo;
using evr::input::olderInputName;
using evr::input::parseControllerData;
using evr::input::parseHandednessName;
using evr::input::pathSuitsAction;
using evr::input::profileHasPath;
using evr::input::SuggestedBinding;
using evr::input::suggestedBindingKey;
using evr::input::XrActionId;
using evr::input::XrActionKind;

namespace {

const std::string kTouchHeader = "[profile]\n\"path\" = \"/interaction_profiles/oculus/touch_controller\"\n";
const std::string kIndexHeader = "[profile]\n\"path\" = \"/interaction_profiles/valve/index_controller\"\n";

const BindingIssue* issueOn(const ControllerData& data, BindingIssueKind kind, std::string_view key) {
    const auto it = std::ranges::find_if(data.issues, [kind, key](const BindingIssue& issue) {
        return issue.kind == kind && issue.key == key;
    });
    return it == data.issues.end() ? nullptr : &*it;
}

const InteractionProfileInfo& touch() {
    return *findInteractionProfile("/interaction_profiles/oculus/touch_controller");
}

const InteractionProfileInfo& index() {
    return *findInteractionProfile("/interaction_profiles/valve/index_controller");
}

} // namespace

TEST_CASE("a profile section becomes full binding paths") {
    const ControllerData data =
        parseControllerData(kTouchHeader + R"("gameplay.left.primary" = "/input/x/click"
"gameplay.right.trigger" = "/input/trigger/value"
"gameplay.right.aim_pose" = "/input/aim/pose"
)");
    CHECK(data.ok());
    CHECK(data.profilePath == "/interaction_profiles/oculus/touch_controller");
    REQUIRE(data.suggested.size() == 3);
    CHECK(*data.find(XrActionId::Primary, Hand::Left) ==
          SuggestedBinding{XrActionId::Primary, Hand::Left, "/user/hand/left/input/x/click"});
    CHECK(data.find(XrActionId::Trigger, Hand::Right)->path == "/user/hand/right/input/trigger/value");
    CHECK(data.find(XrActionId::AimPose, Hand::Right) != nullptr);
    CHECK(data.find(XrActionId::Trigger, Hand::Left) == nullptr);
}

TEST_CASE("keys of the retired menu set in an older player file are skipped without an issue") {
    const ControllerData data =
        parseControllerData(kTouchHeader + R"("gameplay.right.trigger" = "/input/trigger/value"
"menu.right.select" = "/input/trigger/value"
"menu.right.back" = "/input/b/click"
"menu.left.pointer_pose" = "/input/aim/pose"
)");
    CHECK(data.ok());
    REQUIRE(data.suggested.size() == 1);
    CHECK(data.suggested[0].action == XrActionId::Trigger);
}

TEST_CASE("map sections are read as binding text per handedness") {
    const ControllerData data = parseControllerData(kTouchHeader + R"(
[map.right]
"right.trigger.press" = "fire"
[map.left_full_mirror]
"left.trigger.press" = "fire"
)");
    CHECK(data.ok());
    REQUIRE(data.maps.size() == 2);
    CHECK(data.maps.at(Handedness::Right).at("right.trigger.press") == "fire");
    CHECK(data.maps.at(Handedness::LeftButtonAndStickSwap).at("left.trigger.press") == "fire");
}

TEST_CASE("issues carry the file's line numbers") {
    const ControllerData data =
        parseControllerData(kTouchHeader + R"("gameplay.left.primary" = "/input/a/click"
[map.right]
"right.trigger.press" = "fire"
"right.trigger.press" = "jump"
)");
    const BindingIssue* path = issueOn(data, BindingIssueKind::UnknownValue, "gameplay.left.primary");
    REQUIRE(path != nullptr);
    CHECK(path->line == 3);
    const BindingIssue* duplicate = issueOn(data, BindingIssueKind::DuplicateKey, "right.trigger.press");
    REQUIRE(duplicate != nullptr);
    CHECK(duplicate->line == 6);
}

TEST_CASE("unknown keys, sections and profiles are reported") {
    const ControllerData data = parseControllerData(R"("gameplay.left.trigger" = "/input/trigger/value"
[profile]
"path" = "/interaction_profiles/acme/wand"
"gameplay.middle.trigger" = "/input/trigger/value"
"gameplay.left.fire" = "/input/trigger/value"
"gameplay.left.grip" = "/input/squeeze/value"
[map.ambidextrous]
)");
    CHECK(std::ranges::any_of(data.issues, [](const BindingIssue& i) { return i.line == 1; }));
    CHECK(issueOn(data, BindingIssueKind::UnknownValue, "path") != nullptr);
    CHECK(issueOn(data, BindingIssueKind::UnknownKey, "gameplay.middle.trigger") != nullptr);
    CHECK(issueOn(data, BindingIssueKind::UnknownKey, "gameplay.left.fire") != nullptr);
    CHECK(issueOn(data, BindingIssueKind::UnknownKey, "map.ambidextrous") != nullptr);
    // An unknown profile's paths cannot be checked, but its bindings are kept.
    CHECK(data.find(XrActionId::Grip, Hand::Left) != nullptr);
}

TEST_CASE("a missing profile section or path is reported") {
    CHECK_FALSE(parseControllerData("[map.right]\n").ok());
    const ControllerData noPath =
        parseControllerData("[profile]\n\"gameplay.left.grip\" = \"/input/squeeze/value\"\n");
    CHECK(issueOn(noPath, BindingIssueKind::Syntax, "path") != nullptr);
}

TEST_CASE("paths the profile does not have are refused") {
    // Touch has no left A button and no trackpad; Index has no Menu button.
    const ControllerData touchData =
        parseControllerData(kTouchHeader + R"("gameplay.left.primary" = "/input/a/click"
"gameplay.left.thumbstick" = "/input/trackpad"
)");
    CHECK(issueOn(touchData, BindingIssueKind::UnknownValue, "gameplay.left.primary") != nullptr);
    CHECK(issueOn(touchData, BindingIssueKind::UnknownValue, "gameplay.left.thumbstick") != nullptr);
    CHECK(touchData.suggested.empty());
    const ControllerData indexData =
        parseControllerData(kIndexHeader + "\"gameplay.left.menu\" = \"/input/menu/click\"\n");
    CHECK(issueOn(indexData, BindingIssueKind::UnknownValue, "gameplay.left.menu") != nullptr);
    const ControllerData notAnInput =
        parseControllerData(kIndexHeader + "\"gameplay.left.menu\" = \"a/click\"\n");
    CHECK(issueOn(notAnInput, BindingIssueKind::UnknownValue, "gameplay.left.menu") != nullptr);
}

TEST_CASE("a path must suit the action's type") {
    const ControllerData data =
        parseControllerData(kTouchHeader + R"("gameplay.left.aim_pose" = "/input/trigger/value"
"gameplay.left.thumbstick" = "/input/thumbstick/x"
"gameplay.left.trigger" = "/input/thumbstick"
"gameplay.left.haptic" = "/input/grip/pose"
"gameplay.left.primary" = "/input/x/touch"
)");
    for (const std::string_view key : {"gameplay.left.aim_pose", "gameplay.left.thumbstick",
                                       "gameplay.left.trigger", "gameplay.left.haptic"}) {
        CAPTURE(key);
        const BindingIssue* issue = issueOn(data, BindingIssueKind::UnknownValue, key);
        REQUIRE(issue != nullptr);
        CHECK(issue->message.find("does not suit") != std::string::npos);
    }
    // Binding a button action to a touch sensor is legal.
    CHECK(data.find(XrActionId::Primary, Hand::Left) != nullptr);
}

TEST_CASE("path rules of the known profiles") {
    CHECK(profileHasPath(touch(), Hand::Left, "/input/thumbstick"));
    CHECK(profileHasPath(touch(), Hand::Left, "/input/menu/click"));
    CHECK_FALSE(profileHasPath(touch(), Hand::Right, "/input/menu/click"));
    CHECK_FALSE(profileHasPath(touch(), Hand::Left, "/input"));
    CHECK(pathSuitsAction(index(), Hand::Left, "/input/trackpad", XrActionKind::Vector2));
    CHECK(pathSuitsAction(index(), Hand::Left, "/input/trackpad/force", XrActionKind::Boolean));
    CHECK(pathSuitsAction(index(), Hand::Right, "/input/squeeze/force", XrActionKind::Float));
    CHECK(pathSuitsAction(index(), Hand::Right, "/input/trigger", XrActionKind::Boolean));
    CHECK_FALSE(pathSuitsAction(touch(), Hand::Right, "/input/a", XrActionKind::Float));
    CHECK(pathSuitsAction(touch(), Hand::Right, "/output/haptic", XrActionKind::Haptic));
    CHECK_FALSE(pathSuitsAction(touch(), Hand::Right, "/output/haptic", XrActionKind::Boolean));
}

TEST_CASE("the Steam Frame's controllers differ, and its bumper has both names") {
    const InteractionProfileInfo& frame =
        *findInteractionProfile("/interaction_profiles/valve/frame_controller_valve");
    CHECK(profileHasPath(frame, Hand::Right, "/input/x/click"));
    CHECK_FALSE(profileHasPath(frame, Hand::Left, "/input/x/click"));
    CHECK(profileHasPath(frame, Hand::Left, "/input/dpad_up/click"));
    CHECK_FALSE(profileHasPath(frame, Hand::Right, "/input/dpad_up/click"));
    CHECK(profileHasPath(frame, Hand::Left, "/input/view/click"));
    CHECK_FALSE(profileHasPath(frame, Hand::Left, "/input/menu/click"));
    CHECK(profileHasPath(frame, Hand::Right, "/input/menu/click"));
    CHECK_FALSE(profileHasPath(frame, Hand::Right, "/input/system/click"));
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        CHECK(profileHasPath(frame, hand, "/input/shoulder/click"));
        CHECK(profileHasPath(frame, hand, "/input/bumper/click"));
    }
    CHECK(olderInputName("/interaction_profiles/valve/frame_controller_valve",
                         "/user/hand/left/input/shoulder/click") == "/user/hand/left/input/bumper/click");
    CHECK_FALSE(olderInputName("/interaction_profiles/valve/frame_controller_valve",
                               "/user/hand/left/input/a/click"));
    // The Cosmos shoulder buttons were never renamed.
    CHECK_FALSE(olderInputName("/interaction_profiles/htc/vive_cosmos_controller",
                               "/user/hand/left/input/shoulder/click"));
}

TEST_CASE("bindings overlap when they read the same component") {
    constexpr auto kBool = XrActionKind::Boolean;
    constexpr auto kFloat = XrActionKind::Float;
    constexpr auto kAxes = XrActionKind::Vector2;
    CHECK(bindingsOverlap("/user/hand/left/input/y/click", kBool, "/user/hand/left/input/y/click", kBool));
    CHECK(bindingsOverlap("/user/hand/left/input/trigger", kBool, "/user/hand/left/input/trigger/value",
                          kFloat));
    CHECK(bindingsOverlap("/user/hand/left/input/trigger/value", kFloat, "/user/hand/left/input/trigger",
                          kBool));
    CHECK(
        bindingsOverlap("/user/hand/left/input/trackpad", kAxes, "/user/hand/left/input/trackpad/x", kFloat));
    // A stick's axes, its click and its force are separate components.
    CHECK_FALSE(bindingsOverlap("/user/hand/left/input/thumbstick", kAxes,
                                "/user/hand/left/input/thumbstick/click", kBool));
    CHECK_FALSE(bindingsOverlap("/user/hand/left/input/trackpad", kAxes,
                                "/user/hand/left/input/trackpad/force", kBool));
    CHECK_FALSE(
        bindingsOverlap("/user/hand/left/input/y/click", kBool, "/user/hand/left/input/y/touch", kBool));
    CHECK_FALSE(bindingsOverlap("/user/hand/left/input/trigger", kBool,
                                "/user/hand/left/input/trigger_x/click", kBool));
}

// T-106: conflicts name both bound actions and both inputs.

TEST_CASE("conflict: two actions of one set on one input name both actions and both inputs") {
    const ControllerData data =
        parseControllerData(kTouchHeader + R"("gameplay.left.secondary" = "/input/y/click"
"gameplay.left.menu" = "/input/y/click"
)");
    const BindingIssue* issue = issueOn(data, BindingIssueKind::Conflict, "gameplay.left.menu");
    REQUIRE(issue != nullptr);
    CHECK(issue->conflict == BindingConflict::SharedInput);
    CHECK(issue->line == 4);
    CHECK(issue->value == "/user/hand/left/input/y/click");
    CHECK(issue->otherKey == "gameplay.left.secondary");
    CHECK(issue->otherValue == "/user/hand/left/input/y/click");
    CHECK(issue->message ==
          "'gameplay.left.menu' (/user/hand/left/input/y/click) conflicts with "
          "'gameplay.left.secondary' (/user/hand/left/input/y/click): one input would drive "
          "two gameplay actions; 'gameplay.left.menu' is ignored");
    // The earlier action in the set keeps the input.
    CHECK(data.find(XrActionId::Secondary, Hand::Left) != nullptr);
    CHECK(data.find(XrActionId::Menu, Hand::Left) == nullptr);
}

TEST_CASE("conflict: an input and one of its components are one physical input") {
    const ControllerData data =
        parseControllerData(kIndexHeader + R"("gameplay.left.trigger" = "/input/trigger/value"
"gameplay.left.primary" = "/input/trigger"
"gameplay.left.thumbstick" = "/input/trackpad"
"gameplay.left.menu" = "/input/trackpad/force"
)");
    const BindingIssue* issue = issueOn(data, BindingIssueKind::Conflict, "gameplay.left.primary");
    REQUIRE(issue != nullptr);
    CHECK(issue->conflict == BindingConflict::SharedInput);
    CHECK(issue->value == "/user/hand/left/input/trigger");
    CHECK(issue->otherKey == "gameplay.left.trigger");
    CHECK(issue->otherValue == "/user/hand/left/input/trigger/value");
    // The trackpad's axes and its force are separate components: no conflict.
    CHECK(data.issues.size() == 1);
    CHECK(data.find(XrActionId::Menu, Hand::Left) != nullptr);
}

TEST_CASE("the same input on different hands is no conflict") {
    const ControllerData data =
        parseControllerData(kTouchHeader + R"("gameplay.right.trigger" = "/input/trigger/value"
"gameplay.left.trigger" = "/input/trigger/value"
)");
    CHECK(data.ok());
    CHECK(data.suggested.size() == 2);
}

TEST_CASE("handedness and binding key names") {
    CHECK(handednessName(Handedness::Right) == "right");
    CHECK(parseHandednessName("left_button_swap") == Handedness::LeftButtonSwap);
    CHECK(parseHandednessName("left_full_mirror") == Handedness::LeftButtonAndStickSwap);
    CHECK_FALSE(parseHandednessName("left").has_value());
    CHECK(suggestedBindingKey(XrActionId::ThumbstickClick, Hand::Left) == "gameplay.left.thumbstick_click");
    CHECK(suggestedBindingKey(XrActionId::Secondary, Hand::Right) == "gameplay.right.secondary");
}
