#include "features/input/prompt_binds.hpp"

#include "features/input/binding_compiler.hpp"
#include "features/input/button_labels.hpp"
#include "features/input/controller_bindings.hpp"
#include "game/eternal/controller_data.hpp"
#include "game/eternal/usercmd_buttons.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using evr::game::actionForUsercmdButton;
using evr::game::builtinControllerData;
using evr::game::Controller;
using evr::game::GameAction;
using evr::game::Handedness;
using evr::input::BindAnswer;
using evr::input::bindAnswer;
using evr::input::buildBindingProfile;
using evr::input::buttonLabelsFor;
using evr::input::ControllerData;
using evr::input::moveTextIndex;
using evr::input::parseControllerData;
using evr::input::PromptLabelSet;

namespace {

// The game's action table as build 25216728 holds it (.data 0x38A77F0, read statically: bind name, user
// command button), the rows the layer reads at start (vkcore/prompt_hooks.cpp readActionTable).
constexpr std::array<std::pair<std::string_view, int>, 54> kActionTable{{
    {"_jump", 32},
    {"_crouch", 48},
    {"_moveLeft", 49},
    {"_moveRight", 50},
    {"_moveForward", 51},
    {"_moveBack", 52},
    {"_lookUp", 53},
    {"_lookDown", 54},
    {"_lookLeft", 55},
    {"_lookRight", 56},
    {"_sprint", 5},
    {"_objectives", 39},
    {"_power", 38},
    {"_walk", 21},
    {"_attack1", 0},
    {"_attack2", 1},
    {"_use", 3},
    {"_zoom", 4},
    {"_changeWeapon", 6},
    {"_reload", 7},
    {"_mpScoreboard", 7},
    {"_weapnext", 8},
    {"_weapprev", 9},
    {"_weap0", 10},
    {"_weap1", 11},
    {"_weap2", 12},
    {"_weap3", 13},
    {"_weap4", 14},
    {"_weap5", 15},
    {"_weap6", 16},
    {"_weap7", 17},
    {"_weap8", 18},
    {"_weap9", 19},
    {"_bfg", 20},
    {"_dash", 22},
    {"_quickuse", 23},
    {"_quick0", 24},
    {"_quick1", 25},
    {"_quick2", 26},
    {"_quick3", 27},
    {"_inventory", 30},
    {"_nextquickitem", 28},
    {"_prevquickitem", 29},
    {"_groundslam", 33},
    {"_crucible", 34},
    {"_teamSuperCombo", 35},
    {"_trailercam0", 36},
    {"_trailercamMenu", 37},
    {"_altfire", 2},
    {"_speed", 5},
    {"_moveUp", 32},
    {"_moveDown", 48},
    {"_forward", 51},
    {"_back", 52},
}};

// The binds the campaign's texts name, per text (read from the game's English strings, build 25216728; the
// texts themselves are not kept here): every bind a single-player text names is in one of these.
struct TextBinds {
    std::string_view text;
    std::vector<std::string_view> binds;
};

const std::vector<TextBinds>& campaignBinds() {
    static const std::vector<TextBinds> binds{
        {"wall climb tutorial", {"_attack2", "_moveforward", "_moveLeft", "_moveback", "_moveRight"}},
        {"mods loading tip", {"_reload"}},
        {"equipment loading tip", {"_bfg", "_quick0", "_quickuse"}},
        {"dash hint", {"_dash"}},
        {"quick swap loading tip", {"_changeWeapon"}},
        {"Ballista mod loading tip", {"_altfire", "_attack1"}},
        {"chainsaw hint", {"_quick3"}},
        {"Crucible tutorial", {"_crucible", "_attack1"}},
        {"automap hint", {"_inventory"}},
        {"double jump hint", {"_jump"}},
        {"objective marker hint", {"_objectives"}},
        {"use prompt", {"_use"}},
        {"Meat Hook tutorial", {"_zoom"}},
        {"climb prompt", {"_attack2"}},
    };
    return binds;
}

std::optional<int> buttonFor(std::string_view bind) {
    const auto row = std::ranges::find_if(kActionTable, [bind](const auto& r) {
        return std::ranges::equal(r.first, bind, [](char a, char b) { return (a | 0x20) == (b | 0x20); });
    });
    return row != kActionTable.end() ? std::optional<int>(row->second) : std::nullopt;
}

PromptLabelSet labelsFor(Controller controller, Handedness handedness = Handedness::Right) {
    const ControllerData data = parseControllerData(builtinControllerData(controller));
    return evr::input::promptLabelSet(buildBindingProfile(data.maps.at(handedness)).profile,
                                      buttonLabelsFor(data));
}

// What the tooltip text shows for a bind, or "(the game's)".
std::string textFor(std::string_view bind, const PromptLabelSet& labels, bool textPass = true) {
    const std::optional<int> button = buttonFor(bind);
    const std::optional<GameAction> action = button ? actionForUsercmdButton(*button) : std::nullopt;
    const BindAnswer answer = bindAnswer(action, button, textPass, labels);
    return answer.text != nullptr ? *answer.text : std::string("(the game's)");
}

} // namespace

TEST_CASE("the move binds' buttons index the move stick's directions") {
    CHECK(moveTextIndex(51) == 0u);
    CHECK(moveTextIndex(49) == 1u);
    CHECK(moveTextIndex(52) == 2u);
    CHECK(moveTextIndex(50) == 3u);
    CHECK_FALSE(moveTextIndex(48)); // crouch / move down
    CHECK_FALSE(moveTextIndex(53)); // look up
    CHECK_FALSE(moveTextIndex(32)); // jump
}

TEST_CASE("the wall climb tutorial names the move stick for its move binds, not W, A, S and D") {
    const PromptLabelSet touch = labelsFor(Controller::OculusTouch);
    CHECK(textFor("_moveforward", touch) == "Left Stick Up");
    CHECK(textFor("_moveLeft", touch) == "Left Stick Left");
    CHECK(textFor("_moveback", touch) == "Left Stick Down");
    CHECK(textFor("_moveRight", touch) == "Left Stick Right");
    CHECK(textFor("_forward", touch) == "Left Stick Up");
    CHECK(textFor("_back", touch) == "Left Stick Down");
    CHECK(textFor("_attack2", touch) == "Right Stick Click");
    // A full mirror moves with the right stick; a wand with its left trackpad.
    CHECK(textFor("_moveforward", labelsFor(Controller::OculusTouch, Handedness::LeftButtonAndStickSwap)) ==
          "Right Stick Up");
    CHECK(textFor("_moveRight", labelsFor(Controller::ViveWand)) == "Left Trackpad Right");
}

TEST_CASE("the HUD's prompt helpers get no move text (they show one VR key per action)") {
    const PromptLabelSet touch = labelsFor(Controller::OculusTouch);
    CHECK(textFor("_moveforward", touch, false) == "(the game's)");
    const BindAnswer move = bindAnswer(std::nullopt, 51, true, touch);
    CHECK(move.slot == -1);
    // An action keeps its text and slot for both.
    const BindAnswer dash = bindAnswer(GameAction::Dash, 22, false, touch);
    REQUIRE(dash.text != nullptr);
    CHECK(*dash.text == "B");
    CHECK(dash.slot == touch.slot[static_cast<std::size_t>(GameAction::Dash)]);
}

TEST_CASE("the mods tip names the button that swaps the mod") {
    CHECK(textFor("_reload", labelsFor(Controller::OculusTouch)) == "Y");
    CHECK(textFor("_reload", labelsFor(Controller::ValveIndex)) == "Left B");
}

TEST_CASE("every bind the campaign's texts name gets a Touch controller button") {
    for (const Handedness handedness :
         {Handedness::Right, Handedness::LeftButtonSwap, Handedness::LeftButtonAndStickSwap}) {
        const PromptLabelSet touch = labelsFor(Controller::OculusTouch, handedness);
        for (const auto& [text, binds] : campaignBinds()) {
            INFO(text);
            for (const std::string_view bind : binds) {
                INFO(bind);
                CHECK(buttonFor(bind).has_value());
                CHECK(textFor(bind, touch) != "(the game's)");
            }
        }
    }
}

TEST_CASE("binds the game's table does not hold, or no action presses, keep the game's keys") {
    const PromptLabelSet touch = labelsFor(Controller::OculusTouch);
    // _melee (named by a wall climb tutorial variant) and _altattack (named by a Revenant tutorial): not in
    // the game's action table.
    CHECK_FALSE(buttonFor("_melee"));
    CHECK_FALSE(buttonFor("_altattack"));
    CHECK(textFor("_melee", touch) == "(the game's)");
    // Crouch and looking: no action of ours, no move text.
    CHECK(textFor("_crouch", touch) == "(the game's)");
    CHECK(textFor("_lookUp", touch) == "(the game's)");
    // An action with no button on the controller: a wand has no switch weapon mod.
    CHECK(textFor("_reload", labelsFor(Controller::ViveWand)) == "(the game's)");
    // No label set entries at all.
    CHECK(textFor("_moveforward", PromptLabelSet{}) == "(the game's)");
    CHECK(textFor("_dash", PromptLabelSet{}) == "(the game's)");
}
