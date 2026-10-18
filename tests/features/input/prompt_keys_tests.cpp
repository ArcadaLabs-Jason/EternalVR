#include "features/input/prompt_keys.hpp"

#include "features/input/binding_compiler.hpp"
#include "features/input/button_labels.hpp"
#include "features/input/controller_bindings.hpp"
#include "game/eternal/controller_data.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

using evr::game::builtinControllerData;
using evr::game::Controller;
using evr::game::GameAction;
using evr::game::Handedness;
using evr::input::answeredKeyName;
using evr::input::buildBindingProfile;
using evr::input::buttonLabelsFor;
using evr::input::ControllerData;
using evr::input::keyNameAnswer;
using evr::input::KeyNameAnswer;
using evr::input::KeyNameCallers;
using evr::input::keyNameLabel;
using evr::input::kLayerKeyNames;
using evr::input::kPageKeyNames;
using evr::input::kTabKeyNames;
using evr::input::layerKeyIndex;
using evr::input::layerKeyLabel;
using evr::input::pageKeyIndex;
using evr::input::pageKeyLabel;
using evr::input::parseControllerData;
using evr::input::PromptLabelSet;
using evr::input::tabKeyIndex;
using evr::input::tabKeyLabel;

namespace {

PromptLabelSet touchLabels(Handedness handedness = Handedness::Right) {
    const ControllerData data = parseControllerData(builtinControllerData(Controller::OculusTouch));
    return evr::input::promptLabelSet(buildBindingProfile(data.maps.at(handedness)).profile,
                                      buttonLabelsFor(data));
}

// The text the layer gives for the game's key name, or "(the game's)" when it keeps the game's text.
std::string layerText(std::string_view gameKeyName, const PromptLabelSet& labels) {
    const std::optional<std::size_t> index = layerKeyIndex(gameKeyName);
    return index ? layerKeyLabel(*index, labels) : std::string("(the game's)");
}

std::string tabText(std::string_view gameKeyName, const PromptLabelSet& labels) {
    const std::optional<std::size_t> index = tabKeyIndex(gameKeyName);
    return index ? tabKeyLabel(*index, labels) : std::string("(the game's)");
}

} // namespace

TEST_CASE("ENTER names the trigger that selects; ESCAPE the button that goes back") {
    const PromptLabelSet right = touchLabels();
    // The mission-complete screen's "ENTER HOLD TO CONTINUE".
    CHECK(layerText("ENTER", right) == "Right Trigger");
    CHECK(layerText("ESCAPE", right) == "B");
    CHECK(layerText("ENTER", touchLabels(Handedness::LeftButtonSwap)) == "Left Trigger");
}

TEST_CASE("the popups' keys name the buttons of their actions") {
    const PromptLabelSet set = touchLabels();
    const auto action = [&set](GameAction a) {
        return set.text[static_cast<std::size_t>(a)];
    };
    CHECK(layerText("SPACE", set) == action(GameAction::Jump));
    CHECK(layerText("E", set) == action(GameAction::Melee));
    CHECK(layerText("TAB", set) == action(GameAction::Dossier));
    CHECK(layerText("SPACE", set) == "A");
}

TEST_CASE("Q and E as tab keys name the grips") {
    const PromptLabelSet set = touchLabels();
    CHECK(tabText("Q", set) == "LG");
    CHECK(tabText("E", set) == "RG");
    // E is a popup key too, where it is melee's button: the two tables answer different callers.
    CHECK(layerText("E", set) != tabText("E", set));
}

TEST_CASE("other keys keep the game's text") {
    const PromptLabelSet set = touchLabels();
    for (const std::string_view key : {"F", "R", "Q", "enter", "KP_ENTER", "MOUSE1", ""}) {
        INFO(std::string(key));
        CHECK(layerText(key, set) == "(the game's)");
    }
    CHECK(tabText("ENTER", set) == "(the game's)");
    CHECK(layerKeyLabel(kLayerKeyNames.size(), set).empty());
    CHECK(tabKeyLabel(kTabKeyNames.size(), set).empty());
}

TEST_CASE("a control the controller has no name for leaves the game's key") {
    const PromptLabelSet none;
    CHECK(layerText("ENTER", none).empty());
    CHECK(tabText("Q", none).empty());
}

TEST_CASE("every layer key and tab key has its own text") {
    const PromptLabelSet set = touchLabels();
    for (std::size_t i = 0; i < kLayerKeyNames.size(); ++i) {
        INFO(std::string(kLayerKeyNames[i]));
        CHECK(layerKeyIndex(kLayerKeyNames[i]) == i);
        CHECK_FALSE(layerKeyLabel(i, set).empty());
    }
    for (std::size_t i = 0; i < kTabKeyNames.size(); ++i) {
        CHECK(tabKeyIndex(kTabKeyNames[i]) == i);
        CHECK_FALSE(tabKeyLabel(i, set).empty());
    }
}

namespace {

// The return addresses found in build 25216728 (prompts: KeyNameText hook at RVA 0x17E2680; TabList calls at
// 0x1592761, 0x159284B; hold buttons' calls at 0x159F095 (GetKeyString), 0x159FE8F (the command bar's
// update)), at an image base.
constexpr std::uintptr_t kBase = 0x7FF7ACDB0000;
KeyNameCallers steamCallers() {
    KeyNameCallers callers;
    callers.tabList = {kBase + 0x1592761, kBase + 0x159284B};
    callers.getKeyString = kBase + 0x159F095;
    callers.commandBarUpdate = kBase + 0x159FE8F;
    // The loading screen tips bar's format calls (0x4587E1, 0x4589A5), each answered at its return address.
    callers.loadTips = {kBase + 0x4587E6, kBase + 0x4589AA};
    // The Rewards tab's paginator update calls KeyNameText for its arrows at 0xF5FEC4 (A) and 0xF5FFBE (D).
    callers.rewards = {kBase + 0xF5FEC9, kBase + 0xF5FFC3};
    callers.tabListPages = true;
    return callers;
}

// What the KeyNameText detour gives a caller for a key: the text, or "(the game's)".
std::string rawKeyText(std::uintptr_t returnAddress,
                       std::string_view key,
                       const KeyNameCallers& callers,
                       const PromptLabelSet& labels) {
    const std::string* text = keyNameLabel(keyNameAnswer(returnAddress, callers), key, labels);
    return text != nullptr && !text->empty() ? *text : std::string("(the game's)");
}

} // namespace

TEST_CASE("the command bar's hold buttons get the controller button for their raw key text") {
    const KeyNameCallers callers = steamCallers();
    const PromptLabelSet set = touchLabels();
    // The mission-complete screen's "ENTER HOLD TO CONTINUE", from GetKeyString and from the bar's update.
    CHECK(keyNameAnswer(callers.getKeyString, callers) == KeyNameAnswer::LayerKey);
    CHECK(keyNameAnswer(callers.commandBarUpdate, callers) == KeyNameAnswer::LayerKey);
    CHECK(rawKeyText(callers.getKeyString, "ENTER", callers, set) == "Right Trigger");
    CHECK(rawKeyText(callers.commandBarUpdate, "ENTER", callers, set) == "Right Trigger");
    CHECK(rawKeyText(callers.getKeyString, "ESCAPE", callers, set) == "B");
    // A hold button's E is a layer key (melee's button), not the tab lists' next tab.
    CHECK(rawKeyText(callers.getKeyString, "E", callers, set) == layerText("E", set));
    CHECK(rawKeyText(callers.getKeyString, "F", callers, set) == "(the game's)");
}

TEST_CASE("the tab lists' raw key text names the grips") {
    const KeyNameCallers callers = steamCallers();
    const PromptLabelSet set = touchLabels();
    CHECK(keyNameAnswer(callers.tabList[0], callers) == KeyNameAnswer::TabKey);
    CHECK(rawKeyText(callers.tabList[0], "Q", callers, set) == "LG");
    CHECK(rawKeyText(callers.tabList[1], "E", callers, set) == "RG");
    CHECK(rawKeyText(callers.tabList[0], "ENTER", callers, set) == "(the game's)");
}

TEST_CASE("the loading screen's tips bar names the grips that send its Q and E") {
    const KeyNameCallers callers = steamCallers();
    const PromptLabelSet set = touchLabels();
    // "[Q] PREVIOUS | [Right Stick Click] NEXT" before (rig 2026-10-16): Q kept, E named as melee's button,
    // and neither the stick click nor anything else sent E there.
    CHECK(keyNameAnswer(callers.loadTips[0], callers) == KeyNameAnswer::TabKey);
    CHECK(keyNameAnswer(callers.loadTips[1], callers) == KeyNameAnswer::TabKey);
    CHECK(rawKeyText(callers.loadTips[0], "Q", callers, set) == "LG");
    CHECK(rawKeyText(callers.loadTips[1], "E", callers, set) == "RG");
    CHECK(rawKeyText(callers.loadTips[1], "E", callers, set) != layerText("E", set));
    CHECK(rawKeyText(callers.loadTips[0], "ENTER", callers, set) == "(the game's)");
    // A half not found at install answers nothing.
    KeyNameCallers missing = callers;
    missing.loadTips[1] = 0;
    CHECK(keyNameAnswer(callers.loadTips[1], missing) == KeyNameAnswer::Game);
}

TEST_CASE("every other caller of the raw key text keeps the game's text") {
    const KeyNameCallers callers = steamCallers();
    const PromptLabelSet set = touchLabels();
    // KeyNameText's own neighbours (0x17E265B) and a HUD caller (0xF13563) are not prompts the layer answers.
    for (const std::uintptr_t other :
         {kBase + 0x17E265B, kBase + 0xF13563, callers.getKeyString + 1, std::uintptr_t{0}}) {
        CHECK(keyNameAnswer(other, callers) == KeyNameAnswer::Game);
        CHECK(rawKeyText(other, "ENTER", callers, set) == "(the game's)");
    }
    // A part not found at install stays 0 and answers nothing.
    KeyNameCallers missing = callers;
    missing.commandBarUpdate = 0;
    CHECK(keyNameAnswer(callers.commandBarUpdate, missing) == KeyNameAnswer::Game);
    CHECK(keyNameAnswer(callers.getKeyString, missing) == KeyNameAnswer::LayerKey);
    CHECK(keyNameAnswer(0, KeyNameCallers{}) == KeyNameAnswer::Game);
}

TEST_CASE("the Rewards tab's page arrows name the stick that turns its pages") {
    const KeyNameCallers callers = steamCallers();
    const PromptLabelSet set = touchLabels();
    // "A 1/2 D" before: no controller input sent A or D there (rig 2026-10-17).
    CHECK(keyNameAnswer(callers.rewards[0], callers) == KeyNameAnswer::PageKey);
    CHECK(keyNameAnswer(callers.rewards[1], callers) == KeyNameAnswer::PageKey);
    CHECK(rawKeyText(callers.rewards[0], "A", callers, set) == "LS");
    CHECK(rawKeyText(callers.rewards[1], "D", callers, set) == "RS");
    CHECK(rawKeyText(callers.rewards[0], "A", callers, touchLabels(Handedness::LeftButtonSwap)) == "LS");
    // Only the page keys: a tab key or another key from these calls keeps the game's text.
    CHECK(rawKeyText(callers.rewards[0], "Q", callers, set) == "(the game's)");
    CHECK(rawKeyText(callers.rewards[1], "ENTER", callers, set) == "(the game's)");
    // A and D from the hold buttons or the HUD keep the game's text.
    CHECK(rawKeyText(callers.getKeyString, "D", callers, set) == "(the game's)");
    CHECK(rawKeyText(kBase + 0xF13563, "A", callers, set) == "(the game's)");
    // An arrow not found at install answers nothing.
    KeyNameCallers missing = callers;
    missing.rewards = {};
    CHECK(keyNameAnswer(callers.rewards[0], missing) == KeyNameAnswer::Game);
}

TEST_CASE("the page keys have their own text, and every answered key name is known") {
    const PromptLabelSet set = touchLabels();
    for (std::size_t i = 0; i < kPageKeyNames.size(); ++i) {
        INFO(std::string(kPageKeyNames[i]));
        CHECK(pageKeyIndex(kPageKeyNames[i]) == i);
        CHECK_FALSE(pageKeyLabel(i, set).empty());
        CHECK(answeredKeyName(kPageKeyNames[i]) == kPageKeyNames[i]);
    }
    CHECK(pageKeyLabel(kPageKeyNames.size(), set).empty());
    CHECK(answeredKeyName("Q") == std::string_view("Q"));
    CHECK(answeredKeyName("ENTER") == std::string_view("ENTER"));
    CHECK(answeredKeyName("E") == std::string_view("E"));
    CHECK_FALSE(answeredKeyName("Z").has_value());
    CHECK_FALSE(answeredKeyName("").has_value());
}

TEST_CASE("a tab list on A and D names the stick that switches its tabs; on Q and E the grips") {
    const KeyNameCallers callers = steamCallers();
    const PromptLabelSet set = touchLabels();
    // The Dossier's Arsenal, Codex and Challenges pages write their inner tab lists' keys through TabList's
    // two calls, as the Dossier's own tab list writes Q and E.
    CHECK(rawKeyText(callers.tabList[0], "A", callers, set) == "LS");
    CHECK(rawKeyText(callers.tabList[1], "D", callers, set) == "RS");
    CHECK(rawKeyText(callers.tabList[0], "Q", callers, set) == "LG");
    CHECK(rawKeyText(callers.tabList[1], "E", callers, set) == "RG");
    CHECK(rawKeyText(callers.tabList[0], "Z", callers, set) == "(the game's)");
    // The loading screen's tips bar answers as a tab list too; it only ever writes Q and E.
    CHECK(rawKeyText(callers.loadTips[0], "Q", callers, set) == "LG");
}

TEST_CASE("a tab list on A and D keeps the game's keys while no stick sends them") {
    KeyNameCallers callers = steamCallers();
    callers.tabListPages = false; // the tab lists' page-key hook is not in
    const PromptLabelSet set = touchLabels();
    CHECK(keyNameAnswer(callers.tabList[0], callers) == KeyNameAnswer::TabKeyOnly);
    CHECK(rawKeyText(callers.tabList[0], "A", callers, set) == "(the game's)");
    CHECK(rawKeyText(callers.tabList[1], "D", callers, set) == "(the game's)");
    // The grips still send a tab list's Q and E, and the Rewards arrows have their own hook.
    CHECK(rawKeyText(callers.tabList[0], "Q", callers, set) == "LG");
    CHECK(rawKeyText(callers.tabList[1], "E", callers, set) == "RG");
    CHECK(rawKeyText(callers.loadTips[0], "Q", callers, set) == "LG");
    CHECK(rawKeyText(callers.rewards[0], "A", callers, set) == "LS");
}
