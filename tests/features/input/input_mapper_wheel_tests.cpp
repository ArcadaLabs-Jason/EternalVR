#include "features/input/input_mapper.hpp"

#include "features/input/binding_compiler.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/input_frames.hpp"
#include "features/input/player_controller_data.hpp"
#include "game/eternal/controller_data.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

// The weapon wheel on a button of the player's choosing, through a player's controller file and the
// mapper: the turn stick points at the wheel while the button holds it, as it does under the stick's own
// down-hold.

using evr::game::builtinControllerData;
using evr::game::contains;
using evr::game::Controller;
using evr::game::GameAction;
using evr::game::GameActionSet;
using evr::game::Handedness;
using evr::game::kControllers;
using evr::input::Axis2;
using evr::input::BindingProfile;
using evr::input::buildBindingProfile;
using evr::input::ControllerData;
using evr::input::controlMapIssues;
using evr::input::GameInput;
using evr::input::Hand;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::parseControllerData;
using evr::input::placePlayerData;
using evr::test::restingFrame;

namespace {

constexpr float kFrame = 1.0f / 90.0f;
constexpr std::array kAllHandedness{Handedness::Right, Handedness::LeftButtonSwap,
                                    Handedness::LeftButtonAndStickSwap};

// The weapon hand's stick click: melee in every built-in map.
Hand weaponHand(Handedness handedness) {
    return handedness == Handedness::Right ? Hand::Right : Hand::Left;
}

std::string stickClickKey(Hand hand) {
    return std::string(hand == Hand::Left ? "left" : "right") + ".stick_click.press";
}

// A built-in controller file with the weapon hand's stick click given to the weapon wheel in every map,
// line for line as the launcher's controls editor writes it.
std::string wheelOnStickClick(Controller controller) {
    std::string text(builtinControllerData(controller));
    for (const Handedness handedness : kAllHandedness) {
        const std::string header = "[map." + std::string(evr::input::handednessName(handedness)) + "]";
        const std::size_t section = text.find(header);
        REQUIRE(section != std::string::npos);
        const std::string melee = "\"" + stickClickKey(weaponHand(handedness)) + "\" = \"melee\"";
        const std::size_t line = text.find(melee, section);
        REQUIRE(line != std::string::npos);
        text.replace(line, melee.size(),
                     "\"" + stickClickKey(weaponHand(handedness)) + "\" = \"weapon_wheel\"");
    }
    return text;
}

// The player's file in place of the built-in data, as the layer loads a controls folder.
BindingProfile playerProfile(Controller controller, Handedness handedness) {
    std::array<ControllerData, kControllers.size()> data;
    for (std::size_t i = 0; i < kControllers.size(); ++i) {
        data[i] = parseControllerData(builtinControllerData(kControllers[i]));
    }
    ControllerData player = parseControllerData(wheelOnStickClick(controller));
    REQUIRE(player.ok());
    REQUIRE(controlMapIssues(player).empty());
    std::vector<std::string> sources;
    REQUIRE(placePlayerData(data, sources, "mine.toml", std::move(player)).placed);
    const ControllerData& used = data[static_cast<std::size_t>(controller)];
    const auto built = buildBindingProfile(used.maps.at(handedness));
    REQUIRE(built.ok());
    return built.profile;
}

InputFrame frameWith(Hand clickHand, bool click, Hand stickHand, Axis2 stick) {
    InputFrame frame = restingFrame();
    (clickHand == Hand::Left ? frame.left : frame.right).stickClick = click;
    (stickHand == Hand::Left ? frame.left : frame.right).stick = stick;
    return frame;
}

struct WheelRun {
    GameActionSet everDown;
    float turned = 0.0f;
    bool wheelHeldThroughout = true;
    std::vector<Axis2> pointers; // the wheel pointer at the end of each pointing step
};

} // namespace

TEST_CASE("a weapon wheel rebound to the stick click is pointed with the turn stick") {
    for (const Controller controller : {Controller::ValveIndex, Controller::OculusTouch}) {
        for (const Handedness handedness : kAllHandedness) {
            CAPTURE(evr::game::controllerName(controller));
            CAPTURE(static_cast<int>(handedness));
            const BindingProfile profile = playerProfile(controller, handedness);
            REQUIRE(profile.turnStick.has_value());
            const Hand click = weaponHand(handedness);
            const Hand turn = *profile.turnStick;
            InputMapper mapper(profile);

            WheelRun run;
            // The click goes down with the stick centred: the wheel is held, the melee is gone.
            for (int i = 0; i < 30; ++i) {
                const GameInput input = mapper.update(frameWith(click, true, turn, {}), {}, kFrame);
                run.everDown |= input.down;
                run.wheelHeldThroughout =
                    run.wheelHeldThroughout && contains(input.down, GameAction::WeaponWheel);
            }
            // Still holding the click, the turn stick points up-left, right, then up: the wheel's pointer.
            for (const Axis2 stick : {Axis2{-0.7f, 0.7f}, Axis2{1.0f, 0.0f}, Axis2{0.0f, 1.0f}}) {
                GameInput input;
                for (int i = 0; i < 20; ++i) {
                    input = mapper.update(frameWith(click, true, turn, stick), {}, kFrame);
                    run.everDown |= input.down;
                    run.turned += input.turnDegrees;
                    run.wheelHeldThroughout =
                        run.wheelHeldThroughout && contains(input.down, GameAction::WeaponWheel);
                }
                run.pointers.push_back(input.wheelPointer);
            }
            CHECK(run.wheelHeldThroughout);
            CHECK(run.pointers == std::vector<Axis2>{{-0.7f, 0.7f}, {1.0f, 0.0f}, {0.0f, 1.0f}});
            CHECK(run.turned == 0.0f);
            CHECK_FALSE(contains(run.everDown, GameAction::Melee));
            CHECK_FALSE(contains(run.everDown, GameAction::Chainsaw));

            // The click is let go with the stick still up: the wheel closes and picks. That push is not a
            // chainsaw, and the stick coming back is not a quick switch.
            GameActionSet afterRelease;
            for (int i = 0; i < 20; ++i) {
                const GameInput input =
                    mapper.update(frameWith(click, false, turn, {0.0f, 1.0f}), {}, kFrame);
                afterRelease |= input.down;
                CHECK(input.turnDegrees == 0.0f);
            }
            afterRelease |= mapper.update(restingFrame(), {}, kFrame).down;
            afterRelease |= mapper.update(restingFrame(), {}, kFrame).down;
            CHECK(afterRelease.none());

            // The stick is its own again: up is the chainsaw, sideways turns.
            CHECK(contains(mapper.update(frameWith(click, false, turn, {0.0f, 1.0f}), {}, kFrame).down,
                           GameAction::Chainsaw));
            mapper.update(restingFrame(), {}, kFrame);
            CHECK(mapper.update(frameWith(click, false, turn, {1.0f, 0.0f}), {}, kFrame).turnDegrees != 0.0f);
        }
    }
}

TEST_CASE("the stick's own wheel still opens on a down hold with the wheel also on a button") {
    const BindingProfile profile = playerProfile(Controller::ValveIndex, Handedness::Right);
    InputMapper mapper(profile);
    GameInput held;
    for (int i = 0; i < 40; ++i) {
        held = mapper.update(frameWith(Hand::Right, false, Hand::Right, {0.0f, -1.0f}), {}, kFrame);
    }
    CHECK(contains(held.down, GameAction::WeaponWheel));
    const GameInput pointing =
        mapper.update(frameWith(Hand::Right, false, Hand::Right, {-0.7f, 0.7f}), {}, kFrame);
    CHECK(pointing.wheelPointer == Axis2{-0.7f, 0.7f});
    CHECK(pointing.turnDegrees == 0.0f);
    CHECK(contains(mapper.update(restingFrame(), {}, kFrame).released, GameAction::WeaponWheel));
}
