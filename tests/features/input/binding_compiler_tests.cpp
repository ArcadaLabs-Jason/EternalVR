#include "features/input/binding_compiler.hpp"

#include "features/input/binding_text.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <initializer_list>
#include <ostream>
#include <string>
#include <string_view>

using evr::game::GameAction;
using evr::input::BindingBuildResult;
using evr::input::BindingConflict;
using evr::input::BindingIssue;
using evr::input::BindingIssueKind;
using evr::input::BindingMap;
using evr::input::buildBindingProfile;
using evr::input::ButtonBinding;
using evr::input::ButtonInput;
using evr::input::Hand;
using evr::input::parseBindingText;
using evr::input::PressKind;
using evr::input::StickGesture;
using evr::input::StickGestureBinding;
using evr::input::toBindingMap;

namespace {

BindingBuildResult build(std::string_view text) {
    return buildBindingProfile(parseBindingText(text).entries);
}

const BindingIssue* conflictOn(const BindingBuildResult& result, std::string_view key) {
    const auto it = std::ranges::find_if(result.issues, [key](const BindingIssue& issue) {
        return issue.kind == BindingIssueKind::Conflict && issue.key == key;
    });
    return it == result.issues.end() ? nullptr : &*it;
}

bool hasIssue(const BindingBuildResult& result, BindingIssueKind kind, std::string_view key) {
    return std::ranges::any_of(result.issues, [kind, key](const BindingIssue& issue) {
        return issue.kind == kind && issue.key == key;
    });
}

} // namespace

TEST_CASE("valid entries compile into a profile") {
    const BindingBuildResult result = build(R"(weapon_hand = "left"
left.stick.role = "turn"
right.stick.role = "move"
left.trigger.press = "fire"
right.primary.tap = "switch_equipment"
right.primary.hold = "dossier"
left.stick.down_hold = "weapon_wheel"
)");
    CHECK(result.ok());
    CHECK(result.profile.weaponHand == Hand::Left);
    CHECK(result.profile.turnStick == Hand::Left);
    CHECK(result.profile.moveStick == Hand::Right);
    CHECK(result.profile.buttons.size() == 3);
    CHECK(result.profile.stickGestures ==
          std::vector<StickGestureBinding>{{StickGesture::DownHold, GameAction::WeaponWheel}});
}

TEST_CASE("unknown keys and values are reported and skipped") {
    const BindingBuildResult result = build(R"(left.trigger.squeeze = "fire"
right.trigger.press = "fyre"
weapon_hand = "middle"
left.stick.role = "look"
right.grip.press = "weapon_mod"
)");
    CHECK(hasIssue(result, BindingIssueKind::UnknownKey, "left.trigger.squeeze"));
    CHECK(hasIssue(result, BindingIssueKind::UnknownValue, "right.trigger.press"));
    CHECK(hasIssue(result, BindingIssueKind::UnknownValue, "weapon_hand"));
    CHECK(hasIssue(result, BindingIssueKind::UnknownValue, "left.stick.role"));
    CHECK(result.issues.size() == 4);
    // The good entry survives, and a bad weapon hand falls back to the right.
    CHECK(result.profile.buttons ==
          std::vector<ButtonBinding>{
              {Hand::Right, ButtonInput::Grip, PressKind::WhileDown, GameAction::WeaponMod}});
    CHECK(result.profile.weaponHand == Hand::Right);
    CHECK_FALSE(result.profile.moveStick.has_value());
}

TEST_CASE("messages name the offending key and value") {
    const BindingBuildResult result = build("right.trigger.press = \"fyre\"\n");
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues[0].message == "'fyre' is not a game action");
}

TEST_CASE("press plus tap or hold on one input is a conflict") {
    const BindingBuildResult result = build(R"(left.primary.press = "jump"
left.primary.tap = "switch_equipment"
left.primary.hold = "dossier"
)");
    CHECK(hasIssue(result, BindingIssueKind::Conflict, "left.primary.tap"));
    CHECK(hasIssue(result, BindingIssueKind::Conflict, "left.primary.hold"));
    CHECK(result.profile.buttons == std::vector<ButtonBinding>{{Hand::Left, ButtonInput::Primary,
                                                                PressKind::WhileDown, GameAction::Jump}});
    CHECK(result.issues[0].message.find("left.primary.press") != std::string::npos);
}

TEST_CASE("both sticks with the same role is a conflict") {
    const BindingBuildResult result = build(R"(left.stick.role = "move"
right.stick.role = "move"
)");
    CHECK(hasIssue(result, BindingIssueKind::Conflict, "right.stick.role"));
    CHECK(result.profile.moveStick == Hand::Left);
    CHECK_FALSE(result.profile.turnStick.has_value());
}

TEST_CASE("gestures only work on the turn stick") {
    const BindingBuildResult result = build(R"(left.stick.role = "move"
right.stick.role = "turn"
left.stick.up = "chainsaw"
right.stick.up = "chainsaw"
)");
    CHECK(hasIssue(result, BindingIssueKind::Conflict, "left.stick.up"));
    CHECK(result.profile.stickGestures.size() == 1);
}

TEST_CASE("the same action on several inputs is allowed") {
    const BindingBuildResult result = build(R"(right.stick_click.press = "melee"
left.stick_click.press = "melee"
)");
    CHECK(result.ok());
    CHECK(result.profile.buttons.size() == 2);
}

TEST_CASE("none unbinds") {
    const BindingBuildResult result = build("right.trigger.press = \"none\"\n");
    CHECK(result.ok());
    CHECK(result.profile.buttons.empty());
}

TEST_CASE("a profile converts back to the entries it came from") {
    const BindingMap entries = parseBindingText(R"(weapon_hand = "right"
left.stick.role = "move"
right.stick.role = "turn"
right.trigger.press = "fire"
left.primary.hold = "dossier"
right.stick.down_tap = "quick_switch"
)")
                                   .entries;
    CHECK(toBindingMap(buildBindingProfile(entries).profile) == entries);
}

// T-106: every conflict names both bound actions and both input keys, one test per conflict kind.

TEST_CASE("conflict: press with tap names both actions and both inputs") {
    const BindingBuildResult result = build(R"(right.primary.press = "jump"
right.primary.tap = "switch_equipment"
)");
    const BindingIssue* issue = conflictOn(result, "right.primary.tap");
    REQUIRE(issue != nullptr);
    CHECK(issue->conflict == BindingConflict::PressWithTapOrHold);
    CHECK(issue->value == "switch_equipment");
    CHECK(issue->otherKey == "right.primary.press");
    CHECK(issue->otherValue == "jump");
    CHECK(issue->message == "'right.primary.tap' (switch_equipment) conflicts with 'right.primary.press' "
                            "(jump): a press binding fires on every tap and hold; 'right.primary.tap' is "
                            "ignored");
}

TEST_CASE("conflict: press with hold names both actions and both inputs") {
    const BindingBuildResult result = build(R"(left.trigger.press = "equipment"
left.trigger.hold = "flame_belch"
)");
    const BindingIssue* issue = conflictOn(result, "left.trigger.hold");
    REQUIRE(issue != nullptr);
    CHECK(issue->conflict == BindingConflict::PressWithTapOrHold);
    CHECK(issue->value == "flame_belch");
    CHECK(issue->otherKey == "left.trigger.press");
    CHECK(issue->otherValue == "equipment");
    for (const std::string_view part :
         {"left.trigger.hold", "flame_belch", "left.trigger.press", "equipment"}) {
        CHECK(issue->message.find(part) != std::string::npos);
    }
}

TEST_CASE("conflict: a shared stick role names both stick keys and the role") {
    const BindingBuildResult result = build(R"(left.stick.role = "turn"
right.stick.role = "turn"
)");
    const BindingIssue* issue = conflictOn(result, "right.stick.role");
    REQUIRE(issue != nullptr);
    CHECK(issue->conflict == BindingConflict::SharedStickRole);
    CHECK(issue->value == "turn");
    CHECK(issue->otherKey == "left.stick.role");
    CHECK(issue->otherValue == "turn");
    CHECK(issue->message == "'right.stick.role' (turn) conflicts with 'left.stick.role' (turn): both sticks "
                            "are set to 'turn' and only the left one is used; 'right.stick.role' is ignored");
    CHECK(result.profile.turnStick == Hand::Left);
}

TEST_CASE("conflict: a gesture off the turn stick names the gesture and the stick's role") {
    const BindingBuildResult result = build(R"(left.stick.role = "move"
right.stick.role = "turn"
left.stick.down_tap = "quick_switch"
)");
    const BindingIssue* issue = conflictOn(result, "left.stick.down_tap");
    REQUIRE(issue != nullptr);
    CHECK(issue->conflict == BindingConflict::GestureOffTurnStick);
    CHECK(issue->value == "quick_switch");
    CHECK(issue->otherKey == "left.stick.role");
    CHECK(issue->otherValue == "move");
    CHECK(issue->message == "'left.stick.down_tap' (quick_switch) conflicts with 'left.stick.role' (move): "
                            "the left stick is not the turn stick, and stick gestures only work on the turn "
                            "stick; 'left.stick.down_tap' is ignored");
}

TEST_CASE("conflict: a gesture on a stick with no role names the unset role") {
    const BindingBuildResult result = build("right.stick.up = \"chainsaw\"\n");
    const BindingIssue* issue = conflictOn(result, "right.stick.up");
    REQUIRE(issue != nullptr);
    CHECK(issue->conflict == BindingConflict::GestureOffTurnStick);
    CHECK(issue->otherKey == "right.stick.role");
    CHECK(issue->otherValue == "none");
}

TEST_CASE("issues that are not conflicts carry no conflict kind") {
    const BindingBuildResult result = build("right.trigger.press = \"fyre\"\n");
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues[0].conflict == BindingConflict::None);
    CHECK(result.issues[0].otherKey.empty());
}
