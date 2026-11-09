#include "features/input/input_mapper.hpp"

#include "features/input/binding_compiler.hpp"
#include "features/input/binding_text.hpp"
#include "features/input/button_labels.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/input_frames.hpp"
#include "game/eternal/controller_data.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

// The turn stick's directions (T-229) through the compiler, the prompts and the mapper, from the built-in
// right-handed Touch map with a player's lines added.

using evr::game::contains;
using evr::game::GameAction;
using evr::input::Axis2;
using evr::input::BindingBuildResult;
using evr::input::BindingConflict;
using evr::input::BindingIssue;
using evr::input::BindingIssueKind;
using evr::input::BindingMap;
using evr::input::BindingProfile;
using evr::input::buildBindingProfile;
using evr::input::GameInput;
using evr::input::Hand;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::MapperContext;
using evr::input::StickGesture;
using evr::input::StickGestureBinding;
using evr::test::questTouchBindings;
using evr::test::restingFrame;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

using Lines = std::initializer_list<std::pair<std::string_view, std::string_view>>;

// The built-in right-handed Touch map with `lines` set ("none" unbinds, as in a player's file).
BindingMap touchMapWith(Lines lines) {
    BindingMap entries = questTouchBindings();
    for (const auto& [key, value] : lines) {
        entries[std::string(key)] = std::string(value);
    }
    return entries;
}

BindingProfile touchProfileWith(Lines lines) {
    const BindingBuildResult built = buildBindingProfile(touchMapWith(lines));
    REQUIRE(built.ok());
    return built.profile;
}

// The weapon slots on the right stick's eight directions where the game's wheel shows them (the example of
// docs/release/CONTROLS.md; weapon_directions.hpp's default).
const Lines kEightSlots{
    {"right.stick.up", "weapon_slot_1"},    {"right.stick.up_right", "weapon_slot_5"},
    {"right.stick.right", "weapon_slot_2"}, {"right.stick.down_right", "weapon_slot_7"},
    {"right.stick.down", "weapon_slot_3"},  {"right.stick.down_left", "weapon_slot_6"},
    {"right.stick.left", "weapon_slot_4"},  {"right.stick.up_left", "weapon_slot_8"},
    {"right.stick.down_tap", "none"},       {"right.stick.down_hold", "none"},
};

InputFrame rightStick(Axis2 stick) {
    InputFrame frame = restingFrame();
    frame.right.stick = stick;
    return frame;
}

// Stick at `degrees` clockwise from straight up, at full deflection.
Axis2 stickAt(float degrees) {
    const float radians = degrees * std::numbers::pi_v<float> / 180.0f;
    return {std::sin(radians), std::cos(radians)};
}

struct Push {
    int frames = 0;
    int actionFrames = 0;    // frames with `action` down
    float turned = 0.0f;     // degrees, in all
    bool lookCentred = true; // the turn stick as the virtual gamepad's look reads it stayed centred
    bool otherWeaponAction = false;
};

// The right stick held at `stick` for 0.3 s, then let go for 0.1 s.
Push push(InputMapper& mapper, Axis2 stick, GameAction action, MapperContext context = {}) {
    Push run;
    for (int i = 0; i < 27; ++i) {
        const GameInput input = mapper.update(rightStick(stick), context, kFrame);
        ++run.frames;
        run.actionFrames += contains(input.down, action) ? 1 : 0;
        run.turned += std::fabs(input.turnDegrees);
        run.lookCentred = run.lookCentred && mapper.turnStick() == Axis2{};
        run.otherWeaponAction = run.otherWeaponAction || contains(input.down, GameAction::QuickSwitch) ||
                                contains(input.down, GameAction::WeaponWheel);
    }
    for (int i = 0; i < 9; ++i) {
        mapper.update(rightStick({}), context, kFrame);
    }
    return run;
}

const BindingIssue* conflictOn(const BindingBuildResult& result, std::string_view key) {
    const auto it = std::ranges::find_if(result.issues, [key](const BindingIssue& issue) {
        return issue.kind == BindingIssueKind::Conflict && issue.key == key;
    });
    return it == result.issues.end() ? nullptr : &*it;
}

std::string touchPrompt(GameAction action, const BindingProfile& profile) {
    const auto data =
        evr::input::parseControllerData(evr::game::builtinControllerData(evr::game::Controller::OculusTouch));
    return evr::input::actionPromptText(action, profile, evr::input::buttonLabelsFor(data));
}

} // namespace

TEST_CASE("down beside down_tap or down_hold is a conflict, and down is kept") {
    const BindingBuildResult result =
        buildBindingProfile(touchMapWith({{"right.stick.down", "weapon_slot_3"}}));
    REQUIRE(result.issues.size() == 2);
    for (const std::string_view key : {"right.stick.down_tap", "right.stick.down_hold"}) {
        CAPTURE(key);
        const BindingIssue* issue = conflictOn(result, key);
        REQUIRE(issue != nullptr);
        CHECK(issue->conflict == BindingConflict::PressWithTapOrHold);
        CHECK(issue->otherKey == "right.stick.down");
        CHECK(issue->otherValue == "weapon_slot_3");
    }
    CHECK(std::ranges::count(result.profile.stickGestures,
                             StickGestureBinding{StickGesture::Down, GameAction::WeaponSlot3}) == 1);
    CHECK(std::ranges::none_of(result.profile.stickGestures, [](const StickGestureBinding& g) {
        return g.gesture == StickGesture::DownTap || g.gesture == StickGesture::DownHold;
    }));
}

TEST_CASE("a direction on the move stick is a gesture off the turn stick") {
    const BindingBuildResult result =
        buildBindingProfile(touchMapWith({{"left.stick.left", "weapon_slot_4"}}));
    const BindingIssue* issue = conflictOn(result, "left.stick.left");
    REQUIRE(issue != nullptr);
    CHECK(issue->conflict == BindingConflict::GestureOffTurnStick);
}

TEST_CASE("the eight directions compile, and write back as the text they came from") {
    const BindingMap entries = touchMapWith(kEightSlots);
    const BindingBuildResult built = buildBindingProfile(entries);
    REQUIRE(built.ok());
    CHECK(built.profile.stickGestures.size() == 8);
    const BindingMap written = evr::input::toBindingMap(built.profile);
    const BindingMap reread = evr::input::parseBindingText(evr::input::formatBindingText(written)).entries;
    CHECK(reread == written);
    CHECK(written.at("right.stick.up_left") == "weapon_slot_8");
    CHECK(written.at("right.stick.down") == "weapon_slot_3");
    CHECK_FALSE(written.contains("right.stick.down_tap"));
    // The gestures keep the order of their kinds, after the hand's buttons.
    const std::string text = evr::input::formatBindingText(written);
    CHECK(text.find("\"right.stick.up\"") < text.find("\"right.stick.down\""));
    CHECK(text.find("\"right.stick.down\"") < text.find("\"right.stick.left\""));
    CHECK(text.find("\"right.stick.down_left\"") < text.find("\"right.stick.down_right\""));
}

TEST_CASE("prompts name the stick's direction") {
    const BindingProfile profile = touchProfileWith(kEightSlots);
    CHECK(touchPrompt(GameAction::WeaponSlot1, profile) == "Right Stick Up");
    CHECK(touchPrompt(GameAction::WeaponSlot3, profile) == "Right Stick Down");
    CHECK(touchPrompt(GameAction::WeaponSlot4, profile) == "Right Stick Left");
    CHECK(touchPrompt(GameAction::WeaponSlot2, profile) == "Right Stick Right");
    CHECK(touchPrompt(GameAction::WeaponSlot8, profile) == "Right Stick Up Left");
    CHECK(touchPrompt(GameAction::WeaponSlot5, profile) == "Right Stick Up Right");
    CHECK(touchPrompt(GameAction::WeaponSlot6, profile) == "Right Stick Down Left");
    CHECK(touchPrompt(GameAction::WeaponSlot7, profile) == "Right Stick Down Right");
}

TEST_CASE("the built-in map still turns on the stick's left and right, the look reading it") {
    InputMapper mapper(touchProfileWith({}));
    for (const Axis2 side : {Axis2{-1.0f, 0.0f}, Axis2{1.0f, 0.0f}}) {
        const Push run = push(mapper, side, GameAction::WeaponSlot4);
        CHECK(run.turned > 10.0f);
        CHECK_FALSE(run.lookCentred);
        CHECK(run.actionFrames == 0);
    }
    // An up sweep under the claim cones leaves the look its stick, as before.
    mapper.update(rightStick({0.0f, 1.0f}), {}, kFrame);
    CHECK(mapper.turnStick() == Axis2{0.0f, 1.0f});
}

TEST_CASE("a bound side holds its action and the stick's turning back only while it is pushed") {
    InputMapper mapper(touchProfileWith({{"right.stick.left", "weapon_slot_4"}}));
    const Push left = push(mapper, {-1.0f, 0.0f}, GameAction::WeaponSlot4);
    CHECK(left.actionFrames == left.frames);
    CHECK(left.turned == 0.0f);
    CHECK(left.lookCentred);
    // Let go, the slot is up again, and the unbound side turns as before.
    CHECK_FALSE(contains(mapper.update(rightStick({}), {}, kFrame).down, GameAction::WeaponSlot4));
    const Push right = push(mapper, {1.0f, 0.0f}, GameAction::WeaponSlot4);
    CHECK(right.actionFrames == 0);
    CHECK(right.turned > 10.0f);
    CHECK_FALSE(right.lookCentred);
    // The built-in up and down keep working.
    CHECK(push(mapper, {0.0f, 1.0f}, GameAction::Chainsaw).actionFrames > 0);
}

TEST_CASE("weapon slots on all eight directions: each picks its slot, and the stick never turns") {
    InputMapper mapper(touchProfileWith(kEightSlots));
    constexpr std::array<std::pair<float, GameAction>, 8> kDirections{{
        {0.0f, GameAction::WeaponSlot1},
        {45.0f, GameAction::WeaponSlot5},
        {90.0f, GameAction::WeaponSlot2},
        {135.0f, GameAction::WeaponSlot7},
        {180.0f, GameAction::WeaponSlot3},
        {225.0f, GameAction::WeaponSlot6},
        {270.0f, GameAction::WeaponSlot4},
        {315.0f, GameAction::WeaponSlot8},
    }};
    for (const auto& [degrees, slot] : kDirections) {
        const float pushedAt = degrees; // CAPTURE takes a copy: some compilers cannot capture a binding
        CAPTURE(pushedAt);
        const Push run = push(mapper, stickAt(degrees), slot);
        CHECK(run.actionFrames == run.frames);
        CHECK(run.turned == 0.0f);
        CHECK(run.lookCentred);
        CHECK_FALSE(run.otherWeaponAction);
    }
}

TEST_CASE("a side held through a menu's end stays out of play until the stick recentres") {
    InputMapper mapper(touchProfileWith({{"right.stick.left", "weapon_slot_4"}}));
    MapperContext menu;
    menu.menuHold = true;
    mapper.update(rightStick({-1.0f, 0.0f}), menu, kFrame);
    const Push after = push(mapper, {-1.0f, 0.0f}, GameAction::WeaponSlot4);
    CHECK(after.actionFrames == 0);
    CHECK(after.turned == 0.0f);
    // Back in the centre (the push let go), the next push picks.
    CHECK(push(mapper, {-1.0f, 0.0f}, GameAction::WeaponSlot4).actionFrames > 0);
}
