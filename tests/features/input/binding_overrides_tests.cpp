#include "features/input/binding_overrides.hpp"

#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>

using evr::game::GameAction;
using evr::input::applyBindingOverrides;
using evr::input::BindingBuildResult;
using evr::input::BindingIssueKind;
using evr::input::BindingMap;
using evr::input::bindingOverrides;
using evr::input::ButtonBinding;
using evr::input::ButtonInput;
using evr::input::Hand;
using evr::input::PressKind;
using evr::input::resolveBindings;
using evr::test::questTouchBindings;

namespace {

bool hasBinding(const BindingBuildResult& result, const ButtonBinding& wanted) {
    return std::ranges::find(result.profile.buttons, wanted) != result.profile.buttons.end();
}

} // namespace

TEST_CASE("overrides replace, add and unbind") {
    const BindingMap base{{"a", "1"}, {"b", "2"}, {"c", "3"}};
    const BindingMap overrides{{"a", "10"}, {"c", "none"}, {"d", "4"}};
    CHECK(applyBindingOverrides(base, overrides) == BindingMap{{"a", "10"}, {"b", "2"}, {"d", "4"}});
}

TEST_CASE("the overrides of an edit are only what changed") {
    const BindingMap base{{"a", "1"}, {"b", "2"}, {"c", "3"}};
    const BindingMap edited{{"a", "10"}, {"b", "2"}, {"d", "4"}};
    const BindingMap overrides = bindingOverrides(base, edited);
    CHECK(overrides == BindingMap{{"a", "10"}, {"c", "none"}, {"d", "4"}});
    CHECK(applyBindingOverrides(base, overrides) == edited);
}

TEST_CASE("an unedited map has no overrides") {
    const BindingMap base = questTouchBindings();
    CHECK(bindingOverrides(base, base).empty());
}

TEST_CASE("swapping jump and dash through overrides") {
    const BindingBuildResult result = resolveBindings(questTouchBindings(), R"(right.primary.press = "dash"
right.secondary.press = "jump"
)");
    CHECK(result.ok());
    CHECK(hasBinding(result, {Hand::Right, ButtonInput::Primary, PressKind::WhileDown, GameAction::Dash}));
    CHECK(hasBinding(result, {Hand::Right, ButtonInput::Secondary, PressKind::WhileDown, GameAction::Jump}));
    // Everything else still follows the base map.
    CHECK(hasBinding(result, {Hand::Right, ButtonInput::Trigger, PressKind::WhileDown, GameAction::Fire}));
}

TEST_CASE("an override that clashes with the base map is reported at its line") {
    // X already has tap and hold in the base map.
    const BindingBuildResult result = resolveBindings(questTouchBindings(), R"(# my changes
left.primary.press = "automap"
)");
    REQUIRE(result.issues.size() == 2);
    for (const auto& issue : result.issues) {
        CHECK(issue.kind == BindingIssueKind::Conflict);
    }
    // The clash is reported on the base keys the new press makes unreachable.
    CHECK(result.issues[0].key == "left.primary.hold");
    CHECK(result.issues[1].key == "left.primary.tap");
}

TEST_CASE("unbinding the base keys first makes the same change clean") {
    const BindingBuildResult result = resolveBindings(questTouchBindings(), R"(left.primary.tap = "none"
left.primary.hold = "none"
left.primary.press = "automap"
)");
    CHECK(result.ok());
    CHECK(hasBinding(result, {Hand::Left, ButtonInput::Primary, PressKind::WhileDown, GameAction::Automap}));
}

TEST_CASE("text and compile issues are reported together with line numbers") {
    const BindingBuildResult result = resolveBindings(questTouchBindings(), R"(right.trigger.press = "fyre"
right.grip.press weapon_mod
)");
    REQUIRE(result.issues.size() == 2);
    CHECK(result.issues[0].kind == BindingIssueKind::Syntax);
    CHECK(result.issues[0].line == 2);
    CHECK(result.issues[1].kind == BindingIssueKind::UnknownValue);
    CHECK(result.issues[1].line == 1);
}

TEST_CASE("an unbinding override with a mistyped key is reported, not dropped silently") {
    const BindingBuildResult result = resolveBindings(questTouchBindings(), R"(# my changes
rigth.grip.press = "none"
)");
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues[0].kind == BindingIssueKind::UnknownKey);
    CHECK(result.issues[0].key == "rigth.grip.press");
    CHECK(result.issues[0].line == 2);
    // The binding the player meant to remove is still there, and now they are told why.
    CHECK(hasBinding(result, {Hand::Right, ButtonInput::Grip, PressKind::WhileDown, GameAction::WeaponMod}));
}

TEST_CASE("mistyped override keys are reported once, whatever their value") {
    const BindingBuildResult result = resolveBindings(questTouchBindings(), R"(rigth.trigger.press = "fire"
left.grip.press = "none"
)");
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues[0].kind == BindingIssueKind::UnknownKey);
    CHECK(result.issues[0].line == 1);
    // The valid unbinding on the next line still applies.
    CHECK_FALSE(
        hasBinding(result, {Hand::Left, ButtonInput::Grip, PressKind::WhileDown, GameAction::FlameBelch}));
}
