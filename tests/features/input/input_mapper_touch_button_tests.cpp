#include "features/input/binding_compiler.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/family_touch_settings.hpp"
#include "features/input/input_mapper.hpp"
#include "features/input/touch_inputs.hpp"
#include "game/eternal/controller_data.hpp"

#include "features/input/input_frames.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <ostream>
#include <string>

// A touch on a control bound like a button (touch_inputs.hpp, touch_button.hpp) through the mapper: it
// reaches the game, its own control in use ends it, nothing changes with no touch bound, and where a face
// button's touch is the thumb-rest wheel's rest, binding it takes that hand out of the wheel (one rest, one
// job).

using evr::game::contains;
using evr::game::Controller;
using evr::game::GameAction;
using evr::game::Handedness;
using evr::input::BindingProfile;
using evr::input::ButtonInput;
using evr::input::GameInput;
using evr::input::Hand;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::MapperContext;
using evr::input::MapperSettings;
using evr::input::RestWheelMode;
using evr::test::restingFrame;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

int frames(float seconds) {
    return static_cast<int>(seconds / kFrame + 0.5f);
}

BindingProfile compiled(const evr::input::BindingMap& map) {
    const auto built = evr::input::buildBindingProfile(map);
    REQUIRE(built.ok());
    return built.profile;
}

// The built-in right-handed Touch map with `key` = `action` added.
BindingProfile touchWith(const std::string& key, const std::string& action) {
    evr::input::BindingMap map = evr::test::questTouchBindings();
    map[key] = action;
    return compiled(map);
}

evr::input::ControllerData builtin(Controller controller) {
    return evr::input::parseControllerData(evr::game::builtinControllerData(controller));
}

// A family's built-in right-handed map with `key` = `action` added.
BindingProfile familyWith(Controller controller, const std::string& key, const std::string& action) {
    evr::input::BindingMap map = builtin(controller).maps.at(Handedness::Right);
    map[key] = action;
    return compiled(map);
}

// The settings control_map.cpp gives the mapper for a family's built-in data (Face buttons as thumb rest off,
// no scripted input).
MapperSettings familySettings(Controller controller) {
    MapperSettings settings;
    evr::input::applyFamilyTouchSettings(settings, builtin(controller), false, false);
    return settings;
}

struct Run {
    int down = 0;    // frames `action` was down
    int pressed = 0; // times it went down
};

Run feed(InputMapper& mapper,
         const InputFrame& f,
         int count,
         GameAction action,
         Run run = {},
         const MapperContext& context = {}) {
    for (int i = 0; i < count; ++i) {
        const GameInput input = mapper.update(f, context, kFrame);
        run.down += contains(input.down, action) ? 1 : 0;
        run.pressed += contains(input.pressed, action) ? 1 : 0;
    }
    return run;
}

InputFrame frame(bool aTouch, bool aPressed = false) {
    InputFrame f = restingFrame();
    f.right.primaryTouch = aTouch;
    f.right.primaryButton = aPressed;
    return f;
}

} // namespace

TEST_CASE("A touched and let go taps its binding; A pressed jumps and taps nothing") {
    InputMapper mapper(touchWith("right.primary_touch.tap", "weapon_slot_2"));
    feed(mapper, frame(false), 3, GameAction::WeaponSlot2);
    Run run = feed(mapper, frame(true), frames(0.12f), GameAction::WeaponSlot2);
    CHECK(run.pressed == 0);
    run = feed(mapper, frame(false), frames(0.3f), GameAction::WeaponSlot2, run);
    CHECK(run.pressed == 1);

    // A jump: the thumb touches A, presses it, lets go and lifts. Jump fires, slot 2 does not.
    run = feed(mapper, frame(true), 3, GameAction::WeaponSlot2);
    Run jump = feed(mapper, frame(true, true), frames(0.1f), GameAction::Jump);
    CHECK(jump.pressed == 1);
    run = feed(mapper, frame(true), frames(0.1f), GameAction::WeaponSlot2, run);
    run = feed(mapper, frame(false), frames(0.4f), GameAction::WeaponSlot2, run);
    CHECK(run.pressed == 0);
    CHECK(mapper.touchButton(Hand::Right, ButtonInput::PrimaryTouch).stats().ownUse == 1);
}

TEST_CASE("a trigger touch held is a hold until the trigger is pulled") {
    InputMapper mapper(touchWith("right.trigger_touch.press", "automap"));
    InputFrame on = restingFrame();
    on.right.triggerTouch = true;
    feed(mapper, restingFrame(), 3, GameAction::Automap);
    Run run = feed(mapper, on, frames(0.5f), GameAction::Automap);
    CHECK(run.down == frames(0.5f));
    // Pulled past the press threshold: fire, and the touch's press ends on that frame.
    InputFrame pulled = on;
    pulled.right.trigger = 1.0f;
    const GameInput input = mapper.update(pulled, {}, kFrame);
    CHECK(contains(input.down, GameAction::Fire));
    CHECK_FALSE(contains(input.down, GameAction::Automap));
    // Let go of the trigger, the finger still on it: nothing until the finger lifts and lands again.
    run = feed(mapper, on, frames(0.6f), GameAction::Automap);
    CHECK(run.down == 0);
    feed(mapper, restingFrame(), frames(0.6f), GameAction::Automap);
    run = feed(mapper, on, 3, GameAction::Automap);
    CHECK(run.down == 3);
}

TEST_CASE("the trackpad's own controls come from the settings (the Vive wands' stick is the trackpad)") {
    const BindingProfile profile =
        familyWith(Controller::ViveWand, "left.trackpad_touch.tap", "quick_switch");
    InputFrame touched = restingFrame();
    touched.left.trackpadTouch = true;
    InputFrame moving = touched;
    moving.left.stick = {0.0f, 0.8f};

    // With nothing under the trackpad in the settings, moving on it still taps.
    MapperSettings bare;
    for (auto& hand : bare.touchOwnControls) {
        hand.fill({});
    }
    InputMapper plain(profile, bare);
    feed(plain, restingFrame(), 3, GameAction::QuickSwitch);
    Run run = feed(plain, moving, frames(0.15f), GameAction::QuickSwitch);
    run = feed(plain, restingFrame(), frames(0.3f), GameAction::QuickSwitch, run);
    CHECK(run.pressed == 1);

    // With the wands' data the stick out is the trackpad used: no tap; a touch alone still taps.
    InputMapper wand(profile, familySettings(Controller::ViveWand));
    feed(wand, restingFrame(), 3, GameAction::QuickSwitch);
    run = feed(wand, moving, frames(0.15f), GameAction::QuickSwitch);
    run = feed(wand, restingFrame(), frames(0.6f), GameAction::QuickSwitch, run);
    CHECK(run.pressed == 0);
    run = feed(wand, touched, frames(0.15f), GameAction::QuickSwitch, run);
    run = feed(wand, restingFrame(), frames(0.3f), GameAction::QuickSwitch, run);
    CHECK(run.pressed == 1);
}

TEST_CASE("one thumb on A and B: pressing either is no tap of the other's touch") {
    // As the mapper is given them by default and from the Touch data (control_map.cpp).
    for (const bool fromData : {false, true}) {
        CAPTURE(fromData);
        const MapperSettings settings = fromData ? familySettings(Controller::OculusTouch) : MapperSettings{};
        InputFrame both = restingFrame();
        both.right.primaryTouch = true;
        both.right.secondaryTouch = true;

        // A's tap bound; the thumb lies on A and B and presses B (dash): no slot 2.
        InputMapper aTap(touchWith("right.primary_touch.tap", "weapon_slot_2"), settings);
        feed(aTap, restingFrame(), 3, GameAction::WeaponSlot2);
        Run run = feed(aTap, both, frames(0.1f), GameAction::WeaponSlot2);
        InputFrame bPressed = both;
        bPressed.right.secondaryButton = true;
        run = feed(aTap, bPressed, frames(0.1f), GameAction::WeaponSlot2, run);
        run = feed(aTap, both, frames(0.1f), GameAction::WeaponSlot2, run);
        run = feed(aTap, restingFrame(), frames(0.4f), GameAction::WeaponSlot2, run);
        CHECK(run.pressed == 0);
        // The same touch with no press taps (positive control).
        run = feed(aTap, both, frames(0.2f), GameAction::WeaponSlot2);
        run = feed(aTap, restingFrame(), frames(0.4f), GameAction::WeaponSlot2, run);
        CHECK(run.pressed == 1);

        // B's tap bound; the thumb on B and A presses A (jump): a jump, no slot 2.
        InputMapper bTap(touchWith("right.secondary_touch.tap", "weapon_slot_2"), settings);
        feed(bTap, restingFrame(), 3, GameAction::WeaponSlot2);
        run = feed(bTap, both, frames(0.1f), GameAction::WeaponSlot2);
        InputFrame aPressed = both;
        aPressed.right.primaryButton = true;
        const Run jump = feed(bTap, aPressed, frames(0.1f), GameAction::Jump);
        CHECK(jump.pressed == 1);
        run = feed(bTap, both, frames(0.1f), GameAction::WeaponSlot2, run);
        run = feed(bTap, restingFrame(), frames(0.4f), GameAction::WeaponSlot2, run);
        CHECK(run.pressed == 0);
        CHECK(bTap.touchButton(Hand::Right, ButtonInput::SecondaryTouch).stats().ownUse == 1);

        // A's tap bound; the thumb slides from A onto B and presses it within the hold-over: no slot 2.
        InputMapper slide(touchWith("right.primary_touch.tap", "weapon_slot_2"), settings);
        feed(slide, restingFrame(), 3, GameAction::WeaponSlot2);
        run = feed(slide, frame(true), frames(0.15f), GameAction::WeaponSlot2);
        InputFrame onB = restingFrame();
        onB.right.secondaryTouch = true;
        run = feed(slide, onB, 3, GameAction::WeaponSlot2, run);
        onB.right.secondaryButton = true;
        run = feed(slide, onB, frames(0.1f), GameAction::WeaponSlot2, run);
        run = feed(slide, restingFrame(), frames(0.4f), GameAction::WeaponSlot2, run);
        CHECK(run.pressed == 0);
    }
}

TEST_CASE("a landing is voided only just after the control under the touch itself") {
    // As the mapper is given them by default and from the Touch data.
    for (const bool fromData : {false, true}) {
        CAPTURE(fromData);
        const MapperSettings settings = fromData ? familySettings(Controller::OculusTouch) : MapperSettings{};
        // The left stick pushed and let go, X touched 0.15 s later: the stick is under the same thumb but not
        // under X, so the touch counts and its tap fires.
        InputMapper afterStick(touchWith("left.primary_touch.tap", "weapon_slot_2"), settings);
        feed(afterStick, restingFrame(), 3, GameAction::WeaponSlot2);
        InputFrame pushed = restingFrame();
        pushed.left.stick = {0.0f, 0.9f};
        feed(afterStick, pushed, frames(0.3f), GameAction::WeaponSlot2);
        Run run = feed(afterStick, restingFrame(), frames(0.15f), GameAction::WeaponSlot2);
        InputFrame onX = restingFrame();
        onX.left.primaryTouch = true;
        run = feed(afterStick, onX, frames(0.15f), GameAction::WeaponSlot2, run);
        run = feed(afterStick, restingFrame(), frames(0.4f), GameAction::WeaponSlot2, run);
        CHECK(run.pressed == 1);
        CHECK(afterStick.touchButton(Hand::Left, ButtonInput::PrimaryTouch).stats().voided == 0);

        // A pressed and let go, A touched 0.15 s later: the thumb coming back to its button, no tap.
        InputMapper afterA(touchWith("right.primary_touch.tap", "weapon_slot_2"), settings);
        feed(afterA, restingFrame(), 3, GameAction::WeaponSlot2);
        InputFrame pressedA = restingFrame();
        pressedA.right.primaryButton = true;
        feed(afterA, pressedA, frames(0.1f), GameAction::WeaponSlot2);
        run = feed(afterA, restingFrame(), frames(0.15f), GameAction::WeaponSlot2);
        run = feed(afterA, frame(true), frames(0.15f), GameAction::WeaponSlot2, run);
        run = feed(afterA, restingFrame(), frames(0.4f), GameAction::WeaponSlot2, run);
        CHECK(run.pressed == 0);
        CHECK(afterA.touchButton(Hand::Right, ButtonInput::PrimaryTouch).stats().voided == 1);
        // The same touch 0.5 s after the press taps (positive control).
        feed(afterA, pressedA, frames(0.1f), GameAction::WeaponSlot2);
        run = feed(afterA, restingFrame(), frames(0.5f), GameAction::WeaponSlot2);
        run = feed(afterA, frame(true), frames(0.15f), GameAction::WeaponSlot2, run);
        run = feed(afterA, restingFrame(), frames(0.4f), GameAction::WeaponSlot2, run);
        CHECK(run.pressed == 1);
    }
}

TEST_CASE("a map built with a touch already on presses nothing until a fresh landing") {
    // A press binding: nothing while the touch the map found goes on; a fresh landing presses.
    InputMapper press(touchWith("right.primary_touch.press", "automap"));
    Run run = feed(press, frame(true), frames(0.5f), GameAction::Automap);
    CHECK(run.down == 0);
    feed(press, frame(false), frames(0.3f), GameAction::Automap);
    run = feed(press, frame(true), 2, GameAction::Automap);
    CHECK(run.down == 2);

    // A tap binding: that touch lifted is no tap; the next one taps.
    InputMapper tap(touchWith("right.primary_touch.tap", "weapon_slot_2"));
    run = feed(tap, frame(true), frames(0.12f), GameAction::WeaponSlot2);
    run = feed(tap, frame(false), frames(0.3f), GameAction::WeaponSlot2, run);
    CHECK(run.pressed == 0);
    run = feed(tap, frame(true), frames(0.12f), GameAction::WeaponSlot2, run);
    run = feed(tap, frame(false), frames(0.3f), GameAction::WeaponSlot2, run);
    CHECK(run.pressed == 1);

    // The thumb rest bound like a button too.
    InputMapper rest(touchWith("right.thumbrest.press", "automap"));
    InputFrame resting = restingFrame();
    resting.right.thumbRest = true;
    run = feed(rest, resting, frames(0.5f), GameAction::Automap);
    CHECK(run.down == 0);
    feed(rest, restingFrame(), frames(0.5f), GameAction::Automap);
    run = feed(rest, resting, 2, GameAction::Automap);
    CHECK(run.down == 2);

    // A map built with nothing touched presses at the first touch (positive control).
    InputMapper clear(touchWith("right.primary_touch.press", "automap"));
    feed(clear, frame(false), 1, GameAction::Automap);
    run = feed(clear, frame(true), 2, GameAction::Automap);
    CHECK(run.down == 2);
}

TEST_CASE("default guard: no built-in map binds a touch, and touches change nothing then") {
    for (const Controller controller : evr::game::kControllers) {
        CAPTURE(evr::game::controllerName(controller));
        const auto data = builtin(controller);
        for (const auto& [handedness, map] : data.maps) {
            for (const auto& b : compiled(map).buttons) {
                CHECK_FALSE(evr::input::isTouchInput(b.input));
            }
        }
    }
    // The same play with every touch sensor flickering on and off, and with none: the same actions.
    InputMapper still(evr::test::questTouchProfile());
    InputMapper touched(evr::test::questTouchProfile());
    for (int i = 0; i < frames(6.0f); ++i) {
        InputFrame f = restingFrame();
        f.right.primaryButton = (i / 20) % 3 == 0;
        f.right.trigger = (i / 15) % 2 == 0 ? 1.0f : 0.0f;
        f.left.stick = {0.0f, (i / 30) % 2 == 0 ? 1.0f : 0.0f};
        InputFrame t = f;
        for (evr::input::HandState* h : {&t.left, &t.right}) {
            const bool on = (i / 7) % 2 == 0;
            h->primaryTouch = on;
            h->secondaryTouch = !on;
            h->stickTouch = on;
            h->trackpadTouch = !on;
            h->triggerTouch = on;
        }
        const GameInput a = still.update(f, {}, kFrame);
        const GameInput b = touched.update(t, {}, kFrame);
        REQUIRE(a.down == b.down);
    }
}

TEST_CASE("a touch on a control held through a menu fires only after a fresh landing") {
    InputMapper mapper(touchWith("right.secondary_touch.press", "automap"));
    MapperContext menu;
    menu.menuHold = true;
    InputFrame on = restingFrame();
    on.right.secondaryTouch = true;
    feed(mapper, restingFrame(), 3, GameAction::Automap);
    feed(mapper, on, frames(0.3f), GameAction::Automap, {}, menu);
    Run run = feed(mapper, on, frames(0.4f), GameAction::Automap);
    CHECK(run.down == 0);
    feed(mapper, restingFrame(), frames(0.3f), GameAction::Automap);
    run = feed(mapper, on, 2, GameAction::Automap);
    CHECK(run.down == 2);
}

TEST_CASE("one rest, one job: a face button's touch that is the wheel's rest leaves the wheel when bound") {
    // Index with Face buttons as thumb rest: A or B touched is each hand's rest (Index has A and B on both).
    MapperSettings settings;
    settings.restWheel.mode = RestWheelMode::Edge;
    settings.restSensors = {true, true};
    settings.restFaceTouch = true;
    const InputMapper bound(familyWith(Controller::ValveIndex, "right.primary_touch.tap", "quick_switch"),
                            settings);
    CHECK(evr::input::bindsWheelRest(bound.profile(), bound.settings(), Hand::Right));
    CHECK_FALSE(bound.settings().restSensors[1]);
    CHECK(bound.settings().restSensors[0]);
    CHECK(bound.restWheel().usable()); // the left rest picks with the right (turn) stick

    // Not bound: both rests stay the wheel's (positive control).
    const InputMapper free(compiled(builtin(Controller::ValveIndex).maps.at(Handedness::Right)), settings);
    CHECK(free.settings().restSensors[1]);

    // On Touch the thumb rest is the rest: a face button's touch bound leaves the wheel alone.
    MapperSettings touch = settings;
    touch.restIsThumbRest = {true, true};
    const InputMapper onTouch(touchWith("right.primary_touch.tap", "quick_switch"), touch);
    CHECK(onTouch.settings().restSensors[1]);
    CHECK_FALSE(evr::input::bindsWheelRest(onTouch.profile(), onTouch.settings(), Hand::Right));

    // Face touch off: a face button's touch is no rest.
    MapperSettings noFace = settings;
    noFace.restFaceTouch = false;
    const InputMapper index(familyWith(Controller::ValveIndex, "right.primary_touch.tap", "quick_switch"),
                            noFace);
    CHECK(index.settings().restSensors[1]);

    // The stick's touch bound keeps telling the wheel the thumb is elsewhere, and is no rest.
    const InputMapper stick(touchWith("right.stick_touch.tap", "quick_switch"), touch);
    CHECK(stick.settings().restSensors[0]);
    CHECK(stick.settings().restSensors[1]);
}

TEST_CASE("the stick's touch bound still holds a thumb-rest landing back") {
    // The wheel on Touch, the left stick's touch bound to a tap: a left thumb landing on its rest while that
    // touch is on (the thumb on the stick) is still held back, as with nothing bound.
    MapperSettings settings;
    settings.restWheel.mode = RestWheelMode::Edge;
    settings.restSensors = {true, true};
    settings.restIsThumbRest = {true, true};
    for (const bool bound : {true, false}) {
        CAPTURE(bound);
        InputMapper mapper(bound ? touchWith("left.stick_touch.tap", "weapon_slot_2")
                                 : evr::test::questTouchProfile(),
                           settings);
        InputFrame f = restingFrame();
        f.left.thumbRest = true;
        f.left.stickTouch = true;
        mapper.update(restingFrame(), {}, kFrame);
        mapper.update(f, {}, kFrame);
        CHECK(mapper.restWheel().stats().vetoedLandings[0] == 1);
        // Without the stick's touch the landing is not held back (positive control).
        InputMapper clear(bound ? touchWith("left.stick_touch.tap", "weapon_slot_2")
                                : evr::test::questTouchProfile(),
                          settings);
        f.left.stickTouch = false;
        clear.update(restingFrame(), {}, kFrame);
        clear.update(f, {}, kFrame);
        CHECK(clear.restWheel().stats().vetoedLandings[0] == 0);
    }
}
