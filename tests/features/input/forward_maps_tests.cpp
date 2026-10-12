// Forward-compatible control maps (T-127): a player's file keeps working on this version when a later
// version wrote keys this one does not know. Those entries are left out, one issue each; anything else wrong
// still makes the file unusable, and a file with only known keys reads as before.

#include "features/input/binding_compiler.hpp"
#include "features/input/binding_keys.hpp"
#include "features/input/binding_text.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/player_controller_data.hpp"
#include "game/eternal/controller_data.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

using evr::game::builtinControllerData;
using evr::game::Controller;
using evr::game::GameAction;
using evr::game::Handedness;
using evr::game::kControllers;
using evr::input::BindingIssue;
using evr::input::BindingIssueKind;
using evr::input::BindingMap;
using evr::input::buildBindingProfile;
using evr::input::ButtonBinding;
using evr::input::ButtonInput;
using evr::input::ControllerData;
using evr::input::controlMapIssues;
using evr::input::Hand;
using evr::input::isLaterBindingKey;
using evr::input::parseBindingText;
using evr::input::parseControllerData;
using evr::input::PressKind;
using evr::input::usablePlayerData;
using evr::input::UsablePlayerData;
using evr::input::XrActionId;

namespace {

// One control map line: whether it is a later version's key, left out of a player's file with the rest used
// (`skipped`), or any other problem, which makes the file unusable. `alone`: the line is all the map has, so
// left out it would leave the map with no controls, and the file is not used either way.
// The launcher's tests read this table (ForwardMapsTests.cs) and check ControlMapRules against it.
struct MapCase {
    const char* key;
    const char* value;
    bool skipped;
    bool alone = false;
};

// clang-format off
constexpr std::array kMapCases{
    MapCase{"left.futurebutton.tap", "weapon_slot_3", true},
    MapCase{"right.trigger.double_tap", "fire", true},
    MapCase{"right.stick.up_left", "chainsaw", true},
    MapCase{"left.trigger.hold.seconds", "0.4", true},
    MapCase{"right.futurepad.press", "future_action", true},
    MapCase{"left.trigger.squeeze", "fire", true},
    MapCase{"weapon_hands", "right", false},
    MapCase{"left.trigger", "fire", false},
    MapCase{"Left.trigger.press", "fire", false},
    MapCase{"left.trigger-touch.press", "fire", false},
    MapCase{"both.trigger.press", "fire", false},
    MapCase{"wheel.mode", "edge", false},
    MapCase{"right.grip.press", "future_action", false},
    MapCase{"right.stick.down_tap", "future_action", false},
    MapCase{"weapon_hand", "middle", false},
    MapCase{"left.stick.role", "dpad", false},
    MapCase{"left.futurebutton.tap", "weapon_slot_3", true, true},
};
// clang-format on

constexpr std::string_view kHeader = R"([profile]
"path" = "/interaction_profiles/oculus/touch_controller"
"gameplay.left.thumbrest" = "/input/thumbrest/touch"
"gameplay.right.trigger" = "/input/trigger/value"
"gameplay.right.thumbstick" = "/input/thumbstick"
)";

// The known bindings of touchFile's map: the right stick turns, the right trigger fires and the left thumb
// rest taps weapon slot 2.
constexpr std::string_view kKnownLines =
    "\"right.stick.role\" = \"turn\"\n\"right.trigger.press\" = \"fire\"\n"
    "\"left.thumbrest.tap\" = \"weapon_slot_2\"\n";

// A small Touch file: kKnownLines (unless `alone`) plus `extra` lines in the map.
std::string touchFile(std::string_view extra,
                      std::string_view profileExtra = {},
                      std::string_view labels = {},
                      bool alone = false) {
    std::string text(kHeader);
    text += profileExtra;
    text += "[map.right]\n";
    if (!alone) {
        text += kKnownLines;
    }
    text += extra;
    if (!labels.empty()) {
        text += "[labels]\n";
        text += labels;
    }
    return text;
}

std::string line(const MapCase& c) {
    return "\"" + std::string(c.key) + "\" = \"" + std::string(c.value) + "\"\n";
}

// The case's line in a map, beside the bindings of kKnownLines unless the case is `alone`.
BindingMap mapOf(const MapCase& c) {
    BindingMap map;
    if (!c.alone) {
        map = parseBindingText(kKnownLines).entries;
    }
    map.emplace(c.key, c.value);
    return map;
}

bool hasButton(const ControllerData& data, const ButtonBinding& binding) {
    const auto profile = buildBindingProfile(data.maps.at(Handedness::Right)).profile;
    return std::ranges::find(profile.buttons, binding) != profile.buttons.end();
}

const ButtonBinding kRestTap{Hand::Left, ButtonInput::ThumbRest, PressKind::Tap, GameAction::WeaponSlot2};
const ButtonBinding kFire{Hand::Right, ButtonInput::Trigger, PressKind::WhileDown, GameAction::Fire};

} // namespace

TEST_CASE("a binding key of a later version is a hand and two or more lowercase names") {
    for (const char* key : {"left.trigger.press", "left.futurebutton.tap", "right.stick.up_left",
                            "left.trigger.hold.seconds", "right.face5.tap", "left.a.b"}) {
        CAPTURE(key);
        CHECK(isLaterBindingKey(key));
    }
    for (const char* key :
         {"", "weapon_hand", "weapon_hands", "left", "left.trigger", "Left.trigger.press",
          "left.Trigger.press", "both.trigger.press", "left..press", "left.trigger.",
          "left.trigger-touch.press", "left.trigger press.tap", "wheel.mode.edge", "trigger.left.press"}) {
        CAPTURE(key);
        CHECK_FALSE(isLaterBindingKey(key));
    }
}

TEST_CASE("the compiler marks only an unknown key of the later shape as skipped") {
    for (const MapCase& c : kMapCases) {
        CAPTURE(c.key);
        CAPTURE(c.alone);
        const auto result = buildBindingProfile(mapOf(c));
        const bool emptied = c.skipped && c.alone;
        REQUIRE(result.issues.size() == (emptied ? 2 : 1));
        CHECK(result.issues[0].skipped == c.skipped);
        CHECK(result.issues[0].key == c.key);
        if (c.skipped) {
            CHECK(result.issues[0].kind == BindingIssueKind::UnknownKey);
        }
        if (emptied) {
            // Nothing known is left: an issue of its own, not skipped.
            CHECK_FALSE(result.issues[1].skipped);
            CHECK(result.issues[1].kind == BindingIssueKind::UnknownKey);
            CHECK(result.issues[1].key.empty());
            CHECK(result.issues[1].message == "no key in this map is a binding this version knows");
        }
    }
}

TEST_CASE("a player's file with an unknown binding key is used without it; any other problem rejects it") {
    for (const MapCase& c : kMapCases) {
        CAPTURE(c.key);
        CAPTURE(c.value);
        CAPTURE(c.alone);
        const UsablePlayerData read = usablePlayerData(touchFile(line(c), {}, {}, c.alone));
        CHECK(read.usable() == (c.skipped && !c.alone));
        if (!read.usable()) {
            CHECK(read.problems.size() == (c.alone ? 2 : 1));
            if (c.alone) {
                // The map keeps its line, so the log names it among the problems.
                CHECK(read.problems.back().message ==
                      "[map.right] no key in this map is a binding this version knows");
                CHECK(read.data.maps.at(Handedness::Right).contains(c.key));
            }
            CHECK(read.skipped.empty());
            continue;
        }
        REQUIRE(read.skipped.size() == 1);
        CHECK(read.skipped[0].key == c.key);
        CHECK(read.skipped[0].message ==
              "[map.right] '" + std::string(c.key) + "' (= '" + std::string(c.value) +
                  "') is not a binding this version knows (a later version's, or a typo)");
        CHECK_FALSE(read.data.maps.at(Handedness::Right).contains(c.key));
        // The rest of the map applies.
        CHECK(hasButton(read.data, kRestTap));
        CHECK(hasButton(read.data, kFire));
    }
}

TEST_CASE("a map left with no binding this version knows makes the file unusable") {
    const std::string later = line(kMapCases[0]) + line(kMapCases[1]) + line(kMapCases[2]);
    const UsablePlayerData read = usablePlayerData(touchFile(later, {}, {}, true));
    CHECK_FALSE(read.usable());
    CHECK(read.skipped.empty());
    REQUIRE(read.problems.size() == 4);
    CHECK(read.problems.back().message == "[map.right] no key in this map is a binding this version knows");
    // One known binding beside them is enough, even an unbound one.
    const UsablePlayerData one =
        usablePlayerData(touchFile(later + "\"right.grip.press\" = \"none\"\n", {}, {}, true));
    CHECK(one.usable());
    CHECK(one.skipped.size() == 3);
    CHECK(one.data.maps.at(Handedness::Right) == BindingMap{{"right.grip.press", "none"}});
    // An empty map is no change: it was used before and still is.
    CHECK(usablePlayerData(touchFile({}, {}, {}, true)).usable());
}

TEST_CASE("a later version's [profile] input and [labels] name are left out with the rest kept") {
    const std::string text =
        touchFile(line(kMapCases[0]), "\"gameplay.left.trigger_force\" = \"/input/trigger/force\"\n",
                  "\"left.trigger_force\" = \"Force\"\n\"left.thumbrest\" = \"Rest\"\n");
    REQUIRE_FALSE(parseControllerData(text).ok());
    const UsablePlayerData read = usablePlayerData(text);
    CHECK(read.usable());
    const ControllerData& data = read.data;
    const std::vector<BindingIssue>& skipped = read.skipped;
    REQUIRE(skipped.size() == 3);
    CHECK(skipped[0].key == "gameplay.left.trigger_force");
    CHECK(skipped[0].line == 6);
    CHECK(skipped[0].message == "[profile] 'gameplay.left.trigger_force' is not an input this version has");
    CHECK(skipped[1].key == "left.trigger_force");
    CHECK(skipped[1].line == 13);
    CHECK(skipped[1].message == "[labels] 'left.trigger_force' is not an input this version has");
    CHECK(skipped[2].key == "left.futurebutton.tap");
    // Everything known is read as written.
    CHECK(data.find(XrActionId::ThumbRest, Hand::Left) != nullptr);
    CHECK(data.find(XrActionId::Trigger, Hand::Right) != nullptr);
    CHECK(data.suggested.size() == 3);
    CHECK(data.labels == BindingMap{{"left.thumbrest", "Rest"}});
    CHECK(hasButton(data, kRestTap));
}

TEST_CASE("[profile] and [labels] keys of another shape, and unknown values of known keys, still reject") {
    for (const char* profileLine :
         {"\"gameplay.middle.trigger\" = \"/input/trigger/value\"\n",
          "\"gameplay.left.Trigger_force\" = \"/input/trigger/force\"\n",
          "\"futureset.left.trigger\" = \"/input/trigger/value\"\n",
          "\"gameplay.left.primary\" = \"/input/z/click\"\n", "\"gameplay.left.primary\" = \"x/click\"\n"}) {
        CAPTURE(profileLine);
        const UsablePlayerData read = usablePlayerData(touchFile({}, profileLine));
        CHECK(read.skipped.empty());
        CHECK_FALSE(read.usable());
    }
    for (const char* labelLine : {"\"middle.primary\" = \"X\"\n", "\"left.primary.x\" = \"X\"\n",
                                  "\"left.Trigger_force\" = \"F\"\n", "\"left.primary\" = \" \"\n"}) {
        CAPTURE(labelLine);
        const UsablePlayerData read = usablePlayerData(touchFile({}, {}, labelLine));
        CHECK(read.skipped.empty());
        CHECK_FALSE(read.usable());
    }
}

TEST_CASE("an unknown key beside a real problem: the file is still not used") {
    const std::string later = line(kMapCases[0]);
    for (const std::string& extra : {
             later + "\"right.grip.press\" = \"future_action\"\n", // an unknown action of a known key
             later + "\"left.futurebutton.tap\" = \"dash\"\n",     // the unknown key twice
             later + "\"right.trigger.press\" = fire\n",           // a malformed line
             later + "\"left.stick.role\" = \"turn\"\n",           // both sticks turn
             later + "[map.left_custom]\n\"right.trigger.press\" = \"fire\"\n", // a section it does not know
         }) {
        CAPTURE(extra);
        CHECK_FALSE(usablePlayerData(touchFile(extra)).usable());
    }
}

TEST_CASE("a file with only known keys is unchanged: every built-in file has nothing to skip") {
    for (const Controller family : kControllers) {
        const int index = static_cast<int>(family);
        CAPTURE(index);
        const ControllerData before = parseControllerData(builtinControllerData(family));
        const UsablePlayerData read = usablePlayerData(builtinControllerData(family));
        CHECK(read.usable());
        CHECK(read.skipped.empty());
        CHECK(read.data.maps == before.maps);
        CHECK(read.data.suggested == before.suggested);
        CHECK(read.data.labels == before.labels);
        CHECK(read.data.issues.empty());
    }
    // A known file with a real problem keeps every issue it had.
    const std::string bad = touchFile("\"right.grip.press\" = \"jumpp\"\n");
    const UsablePlayerData read = usablePlayerData(bad);
    CHECK(read.skipped.empty());
    CHECK(read.problems.size() == controlMapIssues(parseControllerData(bad)).size());
    CHECK(read.problems.size() == 1);
}
