#include "features/input/binding_compiler.hpp"
#include "features/input/input_mapper.hpp"

#include "features/input/input_frames.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <map>
#include <string>

// A thumb rest bound like a button (touch_button.hpp) through the mapper: it reaches the game, a bound rest
// leaves the thumb-rest wheel (one rest, one job), and a touch held through a menu does not fire after it.

using evr::game::contains;
using evr::game::GameAction;
using evr::input::Axis2;
using evr::input::BindingProfile;
using evr::input::GameInput;
using evr::input::Hand;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::MapperContext;
using evr::input::MapperSettings;
using evr::input::RestWheelLoss;
using evr::input::RestWheelMode;
using evr::test::restingFrame;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

int frames(float seconds) {
    return static_cast<int>(seconds / kFrame + 0.5f);
}

// The built-in right-handed Touch map with `key` = `action` added.
BindingProfile touchWith(const std::string& key, const std::string& action) {
    evr::input::BindingMap map = evr::test::questTouchBindings();
    map[key] = action;
    const auto built = evr::input::buildBindingProfile(map);
    REQUIRE(built.ok());
    return built.profile;
}

InputFrame restFrame(bool left, bool right = false, Axis2 leftStick = {}, Axis2 rightStick = {}) {
    InputFrame f = restingFrame();
    f.left.thumbRest = left;
    f.right.thumbRest = right;
    f.left.stick = leftStick;
    f.right.stick = rightStick;
    return f;
}

struct Run {
    int down = 0;    // frames `action` was down
    int pressed = 0; // times it went down
    int wheelFrames = 0;
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
        run.wheelFrames += contains(input.down, GameAction::WeaponWheel) ? 1 : 0;
    }
    return run;
}

MapperSettings wheelOn(RestWheelMode mode = RestWheelMode::Edge) {
    MapperSettings settings;
    settings.restWheel.mode = mode;
    settings.restSensors = {true, true};
    return settings;
}

// What the map's bound rests do to the wheel, as the control map's log reads it (vkcore/control_map.cpp).
RestWheelLoss lossOf(const InputMapper& mapper, RestWheelMode mode) {
    const BindingProfile& profile = mapper.profile();
    using evr::input::ButtonInput;
    return evr::input::restWheelLoss(mode, {{true, true}, profile.moveStick, profile.turnStick},
                                     {evr::input::bindsInput(profile, Hand::Left, ButtonInput::ThumbRest),
                                      evr::input::bindsInput(profile, Hand::Right, ButtonInput::ThumbRest)});
}

} // namespace

TEST_CASE("a thumb rest bound to a tap quick-switches when the thumb touches and lifts") {
    InputMapper mapper(touchWith("left.thumbrest.tap", "quick_switch"));
    feed(mapper, restFrame(false), 3, GameAction::QuickSwitch);
    Run run = feed(mapper, restFrame(true), frames(0.12f), GameAction::QuickSwitch);
    CHECK(run.pressed == 0);
    run = feed(mapper, restFrame(false), frames(0.3f), GameAction::QuickSwitch, run);
    CHECK(run.pressed == 1);

    // The right rest is not bound: it does nothing.
    run = feed(mapper, restFrame(false, true), frames(0.12f), GameAction::QuickSwitch);
    run = feed(mapper, restFrame(false, false), frames(0.3f), GameAction::QuickSwitch, run);
    CHECK(run.pressed == 0);
}

TEST_CASE("a press binding on a rest is down while the thumb rests") {
    InputMapper mapper(touchWith("right.thumbrest.press", "automap"));
    feed(mapper, restFrame(false), 3, GameAction::Automap);
    Run run = feed(mapper, restFrame(false, true), frames(0.5f), GameAction::Automap);
    CHECK(run.down == frames(0.5f));
    CHECK(run.pressed == 1);
}

TEST_CASE("the built-in maps bind no rest, and leave the wheel its rests") {
    const InputMapper mapper(evr::test::questTouchProfile(), wheelOn());
    for (const auto& b : mapper.profile().buttons) {
        CHECK(b.input != evr::input::ButtonInput::ThumbRest);
    }
    CHECK(mapper.settings().restSensors[0]);
    CHECK(mapper.settings().restSensors[1]);
    CHECK(mapper.restWheel().usable());
}

TEST_CASE("a bound rest leaves the thumb-rest wheel; the other rest still arms") {
    InputMapper mapper(touchWith("left.thumbrest.tap", "quick_switch"), wheelOn());
    CHECK_FALSE(mapper.settings().restSensors[0]);
    CHECK(mapper.settings().restSensors[1]);
    CHECK_FALSE(mapper.restWheel().hands().hasRest[0]);
    CHECK(mapper.restWheel().usable());

    // The left thumb lands and the right (turn) stick is pushed: no pick, the rest is a button.
    feed(mapper, restFrame(false), frames(0.5f), GameAction::WeaponWheel);
    Run run = feed(mapper, restFrame(true), 3, GameAction::WeaponWheel);
    run = feed(mapper, restFrame(true, false, {}, {0.0f, 1.0f}), frames(0.4f), GameAction::WeaponWheel, run);
    CHECK(run.wheelFrames == 0);
    feed(mapper, restFrame(false), frames(0.6f), GameAction::WeaponWheel);

    // The right thumb lands and the left (move) stick is pushed: the wheel picks.
    run = feed(mapper, restFrame(false, true), 3, GameAction::WeaponWheel);
    run = feed(mapper, restFrame(false, true, {1.0f, 0.0f}), frames(0.4f), GameAction::WeaponWheel, run);
    CHECK(run.wheelFrames > 0);
}

TEST_CASE("both rests bound: the thumb-rest wheel is off for these controllers") {
    evr::input::BindingMap map = evr::test::questTouchBindings();
    map["left.thumbrest.tap"] = "quick_switch";
    map["right.thumbrest.hold"] = "automap";
    const InputMapper mapper(evr::input::buildBindingProfile(map).profile, wheelOn());
    CHECK_FALSE(mapper.restWheel().usable());
}

TEST_CASE("a rest still touched when a menu lets go fires only after a fresh landing") {
    InputMapper mapper(touchWith("left.thumbrest.press", "automap"));
    MapperContext menu;
    menu.menuHold = true;
    feed(mapper, restFrame(false), 3, GameAction::Automap);
    // Touched under the menu (the caller drops its actions), still touched after it.
    feed(mapper, restFrame(true), frames(0.4f), GameAction::Automap, {}, menu);
    Run run = feed(mapper, restFrame(true), frames(0.5f), GameAction::Automap);
    CHECK(run.down == 0);
    feed(mapper, restFrame(false), frames(0.3f), GameAction::Automap);
    run = feed(mapper, restFrame(true), 2, GameAction::Automap);
    CHECK(run.down == 2);

    // A tap begun under the menu and lifted after it taps nothing.
    InputMapper tap(touchWith("left.thumbrest.tap", "quick_switch"));
    feed(tap, restFrame(false), 3, GameAction::QuickSwitch);
    feed(tap, restFrame(true), frames(0.1f), GameAction::QuickSwitch, {}, menu);
    run = feed(tap, restFrame(false), frames(0.3f), GameAction::QuickSwitch);
    CHECK(run.pressed == 0);
}

TEST_CASE("which bound rest turns the wheel off, and which leaves it the other rest") {
    SUBCASE("extreme: the move hand's rest, the one that gives turning back, is the one it needs") {
        const InputMapper restore(touchWith("left.thumbrest.tap", "quick_switch"),
                                  wheelOn(RestWheelMode::Extreme));
        CHECK(restore.restWheel().hands().hasRest[1]); // the right rest is still the wheel's ...
        CHECK_FALSE(restore.restWheel().usable());     // ... but extreme cannot work with it alone
        CHECK(lossOf(restore, RestWheelMode::Extreme) == RestWheelLoss::Off);

        const InputMapper turnHand(touchWith("right.thumbrest.tap", "quick_switch"),
                                   wheelOn(RestWheelMode::Extreme));
        CHECK(turnHand.restWheel().usable());
        CHECK(lossOf(turnHand, RestWheelMode::Extreme) == RestWheelLoss::OtherRest);
    }
    SUBCASE("edge: the rest left has no stick to pick with") {
        evr::input::BindingMap map = evr::test::questTouchBindings();
        map["left.thumbrest.tap"] = "quick_switch";
        map["left.stick.role"] = "none";
        const auto built = evr::input::buildBindingProfile(map);
        REQUIRE(built.ok());
        const InputMapper mapper(built.profile, wheelOn());
        CHECK(mapper.restWheel().hands().hasRest[1]);
        CHECK_FALSE(mapper.restWheel().usable());
        CHECK(lossOf(mapper, RestWheelMode::Edge) == RestWheelLoss::Off);
    }
    SUBCASE("edge and full: the other rest picks with the bound hand's stick") {
        const InputMapper mapper(touchWith("left.thumbrest.tap", "quick_switch"),
                                 wheelOn(RestWheelMode::Full));
        CHECK(lossOf(mapper, RestWheelMode::Full) == RestWheelLoss::OtherRest);
    }
    SUBCASE("both bound, none bound, the wheel off") {
        evr::input::BindingMap map = evr::test::questTouchBindings();
        map["left.thumbrest.tap"] = "quick_switch";
        map["right.thumbrest.hold"] = "automap";
        const InputMapper both(evr::input::buildBindingProfile(map).profile, wheelOn());
        CHECK(lossOf(both, RestWheelMode::Edge) == RestWheelLoss::Off);
        const InputMapper none(evr::test::questTouchProfile(), wheelOn());
        CHECK(lossOf(none, RestWheelMode::Edge) == RestWheelLoss::None);
        CHECK(lossOf(both, RestWheelMode::Off) == RestWheelLoss::None);
    }
    SUBCASE("a wheel that could not work anyway loses nothing to a bound rest") {
        // Extreme with no turn stick: off whatever is bound.
        evr::input::BindingMap map = evr::test::questTouchBindings();
        map["left.thumbrest.tap"] = "quick_switch";
        std::erase_if(map, [](const auto& entry) { return entry.first.starts_with("right.stick."); });
        const auto built = evr::input::buildBindingProfile(map);
        REQUIRE(built.ok());
        const InputMapper mapper(built.profile, wheelOn(RestWheelMode::Extreme));
        CHECK(lossOf(mapper, RestWheelMode::Extreme) == RestWheelLoss::None);
    }
}

TEST_CASE("a rest with a tap and a hold fires exactly one of them on every touch") {
    evr::input::BindingMap map = evr::test::questTouchBindings();
    map["left.thumbrest.tap"] = "quick_switch";
    map["left.thumbrest.hold"] = "automap";
    const auto built = evr::input::buildBindingProfile(map);
    REQUIRE(built.ok());
    for (int n = frames(0.2f); n <= frames(0.3f); ++n) {
        CAPTURE(n);
        InputMapper mapper(built.profile);
        feed(mapper, restFrame(false), 3, GameAction::QuickSwitch);
        Run tap = feed(mapper, restFrame(true), n, GameAction::QuickSwitch);
        tap = feed(mapper, restFrame(false), frames(0.4f), GameAction::QuickSwitch, tap);
        InputMapper again(built.profile);
        feed(again, restFrame(false), 3, GameAction::Automap);
        Run hold = feed(again, restFrame(true), n, GameAction::Automap);
        hold = feed(again, restFrame(false), frames(0.4f), GameAction::Automap, hold);
        CHECK(tap.pressed + hold.pressed == 1);
    }
}
