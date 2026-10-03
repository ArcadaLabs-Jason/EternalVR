#include "features/input/input_mapper.hpp"

#include "features/input/input_frames.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

// A menu's hold on gameplay input through the mapper, with the built-in right-handed Touch map (Y tap:
// switch the weapon mod; the right stick: up the chainsaw, a down tap the quick switch, a down hold the
// weapon wheel). Under the hold the caller drops the actions; what matters is what reaches the game after
// it: nothing from a control pressed in the menu, and a fresh press at once.

using evr::game::contains;
using evr::game::GameAction;
using evr::game::GameActionSet;
using evr::input::Axis2;
using evr::input::GameInput;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::MapperContext;
using evr::test::questTouchProfile;
using evr::test::restingFrame;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

struct Run {
    GameActionSet down; // every action down on a frame without the hold
    bool turned = false;
    bool moved = false;
};

// `seconds` of `frame`, under the menu's hold or not, added to `run`.
void step(InputMapper& mapper, const InputFrame& frame, bool hold, float seconds, Run& run) {
    MapperContext context;
    context.menuHold = hold;
    const int frames = static_cast<int>(seconds / kFrame + 0.5f);
    for (int i = 0; i < frames; ++i) {
        const GameInput input = mapper.update(frame, context, kFrame);
        if (!hold) {
            run.down |= input.down;
            run.turned = run.turned || input.turnDegrees != 0.0f;
            run.moved = run.moved || input.move != Axis2{};
        }
    }
}

InputFrame leftY(bool down) {
    InputFrame frame = restingFrame();
    frame.left.secondaryButton = down;
    return frame;
}

InputFrame rightStick(Axis2 stick) {
    InputFrame frame = restingFrame();
    frame.right.stick = stick;
    return frame;
}

InputFrame rightTrigger(float value) {
    InputFrame frame = restingFrame();
    frame.right.trigger = value;
    return frame;
}

} // namespace

TEST_CASE("a Y tap that backs out of a menu does not switch the weapon mod") {
    // Let go on the first frame after the hold (the hold ends with the release).
    InputMapper mapper(questTouchProfile());
    Run run;
    step(mapper, leftY(true), true, 0.1f, run);
    step(mapper, leftY(false), false, 0.5f, run);
    CHECK(run.down.none());

    // Let go under the hold: its tap is the menu's.
    InputMapper inMenu(questTouchProfile());
    Run released;
    step(inMenu, leftY(true), true, 0.1f, released);
    step(inMenu, leftY(false), true, kFrame, released);
    step(inMenu, leftY(false), false, 0.5f, released);
    CHECK(released.down.none());
}

TEST_CASE("Y held through the close does nothing until let go, short or long") {
    InputMapper mapper(questTouchProfile());
    Run run;
    step(mapper, leftY(true), true, 0.1f, run);
    step(mapper, leftY(true), false, 0.05f, run);
    step(mapper, leftY(false), false, 0.2f, run);
    CHECK(run.down.none());

    InputMapper held(questTouchProfile());
    Run longer;
    step(held, leftY(true), true, 0.1f, longer);
    step(held, leftY(true), false, 1.0f, longer); // past the hold time: mission info
    step(held, leftY(false), false, 0.2f, longer);
    CHECK(longer.down.none());
}

TEST_CASE("the turn stick held through the close is neither the wheel, a quick switch nor the chainsaw") {
    InputMapper down(questTouchProfile());
    Run wheel;
    step(down, rightStick({0.0f, -1.0f}), true, 0.2f, wheel);
    step(down, rightStick({0.0f, -1.0f}), false, 0.5f, wheel); // past the wheel's hold time
    step(down, rightStick({}), false, 0.2f, wheel);
    CHECK_FALSE(contains(wheel.down, GameAction::WeaponWheel));
    CHECK_FALSE(contains(wheel.down, GameAction::QuickSwitch));

    // A down sweep that comes back right after the close would be a quick switch.
    InputMapper quick(questTouchProfile());
    Run tap;
    step(quick, rightStick({0.0f, -1.0f}), true, 0.1f, tap);
    step(quick, rightStick({}), false, 0.2f, tap);
    CHECK(tap.down.none());

    InputMapper up(questTouchProfile());
    Run chainsaw;
    step(up, rightStick({0.0f, 1.0f}), true, 0.2f, chainsaw);
    step(up, rightStick({0.0f, 1.0f}), false, 0.5f, chainsaw);
    CHECK(chainsaw.down.none());

    InputMapper side(questTouchProfile());
    Run turn;
    step(side, rightStick({1.0f, 0.0f}), true, 0.2f, turn);
    step(side, rightStick({1.0f, 0.0f}), false, 0.5f, turn);
    CHECK_FALSE(turn.turned);
    CHECK(side.turnStick() == Axis2{}); // the virtual gamepad's look reads it too
    step(side, rightStick({}), false, 0.1f, turn);
    step(side, rightStick({1.0f, 0.0f}), false, 0.1f, turn);
    CHECK(turn.turned);
    CHECK(side.turnStick() == Axis2{1.0f, 0.0f});
}

TEST_CASE("the trigger that clicked Resume fires only when pulled again") {
    InputMapper mapper(questTouchProfile());
    Run run;
    step(mapper, rightTrigger(1.0f), true, 0.1f, run);
    step(mapper, rightTrigger(1.0f), false, 1.0f, run);
    CHECK(run.down.none());
    step(mapper, rightTrigger(0.0f), false, 0.1f, run);
    MapperContext context;
    CHECK(contains(mapper.update(rightTrigger(1.0f), context, kFrame).down, GameAction::Fire));
}

TEST_CASE("the move stick held through the close walks only after it comes back") {
    InputMapper mapper(questTouchProfile());
    InputFrame forward = restingFrame();
    forward.left.stick = {0.0f, 1.0f};
    Run run;
    step(mapper, forward, true, 0.1f, run);
    step(mapper, forward, false, 0.5f, run);
    CHECK_FALSE(run.moved);
    step(mapper, restingFrame(), false, 0.1f, run);
    step(mapper, forward, false, 0.1f, run);
    CHECK(run.moved);
}

TEST_CASE("a press made after the close works at once") {
    InputMapper mapper(questTouchProfile());
    Run run;
    step(mapper, leftY(true), true, 0.1f, run);
    step(mapper, leftY(false), true, 0.1f, run);
    step(mapper, restingFrame(), false, 0.1f, run);
    CHECK(run.down.none());

    // The trigger fires on the frame it is pulled.
    MapperContext context;
    CHECK(contains(mapper.update(rightTrigger(1.0f), context, kFrame).down, GameAction::Fire));
    step(mapper, restingFrame(), false, 0.1f, run);

    Run fresh;
    step(mapper, leftY(true), false, 0.1f, fresh);
    step(mapper, leftY(false), false, 0.1f, fresh);
    CHECK(contains(fresh.down, GameAction::SwitchWeaponMod));

    Run stick;
    step(mapper, rightStick({0.0f, -1.0f}), false, 0.1f, stick);
    step(mapper, rightStick({}), false, 0.1f, stick);
    CHECK(contains(stick.down, GameAction::QuickSwitch));
}

TEST_CASE("a press that starts on the very frame the hold ends works") {
    InputMapper mapper(questTouchProfile());
    Run run;
    step(mapper, restingFrame(), true, 0.1f, run);
    step(mapper, leftY(true), false, 0.1f, run);
    step(mapper, leftY(false), false, 0.1f, run);
    CHECK(contains(run.down, GameAction::SwitchWeaponMod));
}

TEST_CASE("under the hold the mapper still reads the controllers for the menu") {
    // The Menu tap pauses (it closes the pause menu), whatever else the caller drops.
    InputMapper mapper(questTouchProfile());
    MapperContext context;
    context.menuHold = true;
    InputFrame menu = restingFrame();
    menu.left.menuButton = true;
    mapper.update(menu, context, kFrame);
    mapper.update(menu, context, kFrame);
    CHECK(contains(mapper.update(restingFrame(), context, kFrame).down, GameAction::Pause));
}
